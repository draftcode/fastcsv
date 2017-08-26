/* License: BSD 2-Clause License {{{

 Copyright (c) 2013, Masaya SUZUKI <draftcode@gmail.com>
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

 1. Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.

 THIS SOFTWARE IS PROVIDED BY THE FREEBSD PROJECT ``AS IS'' AND ANY EXPRESS
 OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 NO EVENT SHALL THE FREEBSD PROJECT OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 }}} */
#include <Python.h>

typedef enum {
  UniversalNewline,
  LF,
  CR,
  CRLF,
} NewlineMode;

typedef struct {
  PyObject_HEAD
  PyObject *fileobj;
  PyObject *readbuf;
  Py_ssize_t readbuf_start;
  unsigned char entered;
  NewlineMode newline_mode;

  Py_ssize_t cell_cap;
  PyObject **cells;
  Py_ssize_t content_cap;
  PyObject **contents;

  PyObject *read_string, *read_arg;
} Reader;

static unsigned char
ParseNewlineMode(PyObject *newline, NewlineMode *newline_mode) {
  if (!newline || newline == Py_None) {
    *newline_mode = UniversalNewline;
  } else {
    unsigned char free_newline = 0;
#if PY_MAJOR_VERSION >= 3
    // Do not do bytes to unicode conversion in Python3. We expect the caller to
    // use unicode strings consistently in Python3.
#else
    if (PyString_Check(newline)) {
      PyObject *decoded = PyUnicode_FromObject(newline);
      if (!decoded) {
        PyErr_SetString(PyExc_ValueError, "newline kwarg is invalid");
        return 0;
      }
      free_newline = 1;
      newline = decoded;
    }
#endif
    if (PyUnicode_Check(newline)) {
      if (PyUnicode_GET_SIZE(newline) == 1) {
        if (PyUnicode_AS_UNICODE(newline)[0] == '\r') {
          *newline_mode = CR;
        } else if (PyUnicode_AS_UNICODE(newline)[0] == '\n') {
          *newline_mode = LF;
        } else {
          PyErr_SetString(PyExc_ValueError, "newline kwarg is invalid");
          return 0;
        }
      } else if (PyUnicode_GET_SIZE(newline) == 2 &&
          PyUnicode_AS_UNICODE(newline)[0] == '\r' &&
          PyUnicode_AS_UNICODE(newline)[1] == '\n') {
        *newline_mode = CRLF;
      } else {
        PyErr_SetString(PyExc_ValueError, "newline kwarg is invalid");
        return 0;
      }
    }

    if (free_newline) {
      Py_DECREF(newline);
    }
  }
  return 1;
}

static int
Reader_init(Reader *self, PyObject *args, PyObject *kwds) {
  static char *kwlist[] = {"fileobj", "newline",   NULL};
  PyObject *fileobj = NULL;
  PyObject *newline = NULL;
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|O", kwlist,
                                   &fileobj,
                                   &newline))
    goto error;

  if (!ParseNewlineMode(newline, &(self->newline_mode))) goto error;

  self->cell_cap = 256;
  self->cells = PyMem_New(PyObject *, self->cell_cap);
  if (!self->cells) goto error;
  self->content_cap = 8;
  self->contents = PyMem_New(PyObject *, self->content_cap);
  if (!self->contents) goto error;

#if PY_MAJOR_VERSION >= 3
  self->read_string = PyUnicode_FromString("read");
#else
  self->read_string = PyString_FromString("read");
#endif
  if (!self->read_string) goto error;
  self->read_arg = PyLong_FromLong(1024);
  if (!self->read_arg) goto error;

  self->entered = 0;
  self->readbuf = NULL;
  self->readbuf_start = 0;

  {
    PyObject *tmp = self->fileobj;
    Py_INCREF(fileobj);
    self->fileobj = fileobj;
    Py_XDECREF(tmp);
  }

  return 0;
error:
  Py_XDECREF(self->read_string);
  Py_XDECREF(self->read_arg);
  if (self->cells) PyMem_Del(self->cells);
  if (self->contents) PyMem_Del(self->contents);
  return -1;
}

static void
Reader_dealloc(Reader *self) {
  Py_XDECREF(self->fileobj);
  Py_XDECREF(self->readbuf);
  Py_XDECREF(self->read_string);
  Py_XDECREF(self->read_arg);
  if (self->cells) PyMem_Del(self->cells);
  if (self->contents) PyMem_Del(self->contents);
}

static PyObject *
Reader___enter__(Reader *self, PyObject *args) {
  if (self->entered) {
    PyErr_SetString(PyExc_Exception, "recursively entered");
    return NULL;
  }
  self->entered = 1;
  Py_INCREF(self);
  return (PyObject *) self;
}

static PyObject *
Reader___exit__(Reader *self, PyObject *args) {
  if (!self->entered) {
    PyErr_SetString(PyExc_Exception, "have not entered but tried to exit");
    return NULL;
  }
  if (PyObject_HasAttrString(self->fileobj, "close")) {
    PyObject_CallMethod(self->fileobj, "close", NULL);
  }
  Py_RETURN_NONE;
}

typedef enum {
  SEE_SPLITTER,
  SEE_LINEENDING,
  SEE_QUOTE,
  SEE_EOL,
  SEE_CR_EOL,
} BreakReason;

/* Support function: Seek
   Takes unicode buffer of a line and returns an Unicode object and break
   reason. It finds splitter(',') or lineending or quote('"'), and returns an
   Unicode object of the substring from start to just before the found char.
 */
static BreakReason
Seek(Reader *self, PyObject **ppret, unsigned char skip_splitter,
     unsigned char contain_newline)
{
  /* Pre-condition: (readbuf != NULL && readbuf_start < end && ppret != NULL)
   */
  const Py_UNICODE *buf = PyUnicode_AS_UNICODE(self->readbuf);
  Py_ssize_t curr = self->readbuf_start;
  Py_ssize_t end = PyUnicode_GET_SIZE(self->readbuf);
  Py_ssize_t skip = 0;
  BreakReason reason = SEE_EOL;

  for (; curr < end; curr++) {
    if (buf[curr] == '"') {
      reason = SEE_QUOTE;
      skip = 1;
      break;
    } else if (!skip_splitter && buf[curr] == ',') {
      reason = SEE_SPLITTER;
      skip = 1;
      break;
    } else if (!contain_newline) {
      if ((self->newline_mode == UniversalNewline ||
           self->newline_mode == CRLF) &&
          buf[curr] == '\r' &&
          ((curr+1 < end && buf[curr+1] == '\n') || curr+1 == end)) {
        if (curr+1 < end) {
          reason = SEE_LINEENDING;
          skip = 2;
        } else {
          reason = SEE_CR_EOL;
          skip = 1;
        }
        break;
      } else if ((self->newline_mode == UniversalNewline ||
                  self->newline_mode == CR) &&
                 buf[curr] == '\r') {
        reason = SEE_LINEENDING;
        skip = 1;
        break;
      } else if ((self->newline_mode == UniversalNewline ||
                  self->newline_mode == LF) &&
                 buf[curr] == '\n') {
        reason = SEE_LINEENDING;
        skip = 1;
        break;
      }
    }
  }
  *ppret = PyUnicode_FromUnicode(buf + self->readbuf_start,
                                 curr - self->readbuf_start);
  self->readbuf_start = curr + skip;
  return reason;
  /* Post-condition: **ppret can be NULL && readbuf_start <= end */
}

/* Support function: JoinAndClear
   Takes an array of PyUnicode * and join them into one PyUnicode*.
   Every object in the array is DECREFed.
 */
static PyObject *
JoinAndClear(PyObject **contents, Py_ssize_t content_count) {
  PyObject *ret;
  Py_ssize_t retsize, bufidx;
  Py_UNICODE *buf;
  Py_ssize_t i, j;

  if (content_count == 1) {
    ret = contents[0];
    contents[0] = NULL;
    return ret;
  }

  retsize = 0;
  for (i = 0; i < content_count; i++) {
    retsize += PyUnicode_GET_SIZE(contents[i]);
  }

  ret = PyUnicode_FromUnicode(NULL, retsize);
  if (ret == NULL) return PyErr_NoMemory();
  buf = PyUnicode_AS_UNICODE(ret);

  bufidx = 0;
  for (i = 0; i < content_count; i++) {
    Py_UNICODE *cellbuf = PyUnicode_AS_UNICODE(contents[i]);
    for (j = 0; j < PyUnicode_GET_SIZE(contents[i]); j++) {
      buf[bufidx++] = cellbuf[j];
    }
  }
  return ret;
}

/* Support function: PackRowAndClear
   This function receives an array of PyUnicode* and pack them into a list.
   Every element in the array is (virtually) DECREFed.
 */
static PyObject *
PackRowAndClear(PyObject **cells, Py_ssize_t cell_count) {
  PyObject *ret;
  Py_ssize_t i;

  ret = PyList_New(cell_count);
  if (ret == NULL) return PyErr_NoMemory();

  for (i = 0; i < cell_count; i++) {
    PyList_SET_ITEM(ret, i, cells[i]);
    cells[i] = NULL;
  }
  return ret;
}

typedef enum {
  EXPECT_CELL,
  EOL_CONTINUE,
  IN_QUOTE,
  OUT_QUOTE,
} ReaderState;

#define CHECK_SIZE(mem, cap, count) \
  do { \
    if (count == cap) { \
      PyObject **tmp; \
      cap *= 2; \
      tmp = PyMem_Resize(mem, PyObject *, cap); \
      if (!tmp) { \
        PyErr_NoMemory(); \
        Py_XDECREF(ret); \
        ret = NULL; \
        goto free_and_exit; \
      } \
    } \
  } while (0)

static PyObject *
Reader_iternext(Reader *self) {
  Py_ssize_t cell_count, content_count;
  PyObject *ret = NULL;
  ReaderState state;
  unsigned char skip_lf_if_exists = 0;

  cell_count = 0;
  content_count = 0;

  state = EXPECT_CELL;
  ret = NULL;
  while (1) {
    unsigned char skip_splitter, contain_newline;
    BreakReason break_reason;
    PyObject *cellstr;

    if (!self->readbuf ||
        self->readbuf_start >= PyUnicode_GET_SIZE(self->readbuf))
    {
      Py_XDECREF(self->readbuf);
      self->readbuf = PyObject_CallMethodObjArgs(self->fileobj,
                                                 self->read_string,
                                                 self->read_arg,
                                                 NULL);
      if (self->readbuf == NULL || PyUnicode_GET_SIZE(self->readbuf) == 0) {
        if (skip_lf_if_exists) {
          /* If this flag be set, it expects skip \r char if exists. In this
             case there is no character left, and a row should be returned. */
          goto return_row;
        } else if (!PyErr_Occurred() && (cell_count != 0 || state == IN_QUOTE)) {
          PyErr_SetString(PyExc_IOError, "unexpected end of data");
          Py_XDECREF(ret);
          ret = NULL;
        }
        goto free_and_exit;
      }
      self->readbuf_start = 0;
    }

    if (skip_lf_if_exists) {
      if (PyUnicode_AS_UNICODE(self->readbuf)[self->readbuf_start] == '\n') {
        self->readbuf_start++;
      }
      goto return_row;
    }

    skip_splitter = (state == IN_QUOTE);
    contain_newline = (state == IN_QUOTE);
    break_reason = Seek(self, &cellstr, skip_splitter, contain_newline);
    switch (state) {
      case EXPECT_CELL:
        switch (break_reason) {
          case SEE_SPLITTER:
          case SEE_LINEENDING:
          case SEE_CR_EOL:
            CHECK_SIZE(self->cells, self->cell_cap, cell_count);
            self->cells[cell_count++] = cellstr;
            if (break_reason == SEE_LINEENDING) goto return_row;
            if (break_reason == SEE_CR_EOL) skip_lf_if_exists = 1;
            break;

          case SEE_QUOTE:
            if (PyUnicode_GET_SIZE(cellstr) != 0) {
              PyErr_SetString(PyExc_ValueError, "string before quote");
              Py_DECREF(cellstr);
              goto free_and_exit;
            }
            Py_DECREF(cellstr);
            state = IN_QUOTE;
            break;

          case SEE_EOL:
            CHECK_SIZE(self->contents, self->content_cap, content_count);
            self->contents[content_count++] = cellstr;
            state = EOL_CONTINUE;
            break;
        }
        break;

      case EOL_CONTINUE:
        switch (break_reason) {
          case SEE_SPLITTER:
          case SEE_LINEENDING:
          case SEE_CR_EOL:
            CHECK_SIZE(self->contents, self->content_cap, content_count);
            self->contents[content_count++] = cellstr;
            cellstr = JoinAndClear(self->contents, content_count);
            if (!cellstr) goto free_and_exit;
            content_count = 0;
            CHECK_SIZE(self->cells, self->cell_cap, cell_count);
            self->cells[cell_count++] = cellstr;
            if (break_reason == SEE_LINEENDING) goto return_row;
            if (break_reason == SEE_CR_EOL) skip_lf_if_exists = 1;
            state = EXPECT_CELL;
            break;

          case SEE_QUOTE:
            PyErr_SetString(PyExc_ValueError, "string before quote");
            Py_DECREF(cellstr);
            goto free_and_exit;

          case SEE_EOL:
            CHECK_SIZE(self->contents, self->content_cap, content_count);
            self->contents[content_count++] = cellstr;
            break;
        }
        break;

      case IN_QUOTE:
        switch (break_reason) {
          case SEE_SPLITTER:
          case SEE_LINEENDING:
          case SEE_CR_EOL:
            PyErr_SetString(PyExc_Exception, "programming error");
            Py_DECREF(cellstr);
            goto free_and_exit;

          case SEE_QUOTE:
            CHECK_SIZE(self->contents, self->content_cap, content_count);
            self->contents[content_count++] = cellstr;
            state = OUT_QUOTE;
            break;

          case SEE_EOL:
            CHECK_SIZE(self->contents, self->content_cap, content_count);
            self->contents[content_count++] = cellstr;
            break;
        }
        break;

      case OUT_QUOTE:
        if (PyUnicode_GET_SIZE(cellstr) != 0) {
          PyErr_SetString(PyExc_ValueError, "string after quote");
          Py_DECREF(cellstr);
          goto free_and_exit;
        }
        Py_DECREF(cellstr);

        switch (break_reason) {
          case SEE_SPLITTER:
          case SEE_LINEENDING:
          case SEE_CR_EOL:
            cellstr = JoinAndClear(self->contents, content_count);
            if (!cellstr) goto free_and_exit;
            content_count = 0;
            CHECK_SIZE(self->cells, self->cell_cap, cell_count);
            self->cells[cell_count++] = cellstr;
            if (break_reason == SEE_LINEENDING) goto return_row;
            if (break_reason == SEE_CR_EOL) skip_lf_if_exists = 1;
            state = EXPECT_CELL;
            break;

          case SEE_QUOTE:
            CHECK_SIZE(self->contents, self->content_cap, content_count);
            self->contents[content_count++] = PyUnicode_FromString("\"");
            state = IN_QUOTE;
            break;

          case SEE_EOL:
            PyErr_SetString(PyExc_Exception, "programming error");
            Py_DECREF(cellstr);
            goto free_and_exit;
        }
        break;
    }
  }

return_row:
  ret = PackRowAndClear(self->cells, cell_count);
  if (!ret) goto free_and_exit;
  cell_count = 0;

free_and_exit:
  {
    int i;
    for (i = 0; i < cell_count; i++) Py_DECREF(self->cells[i]);
    for (i = 0; i < content_count; i++) Py_DECREF(self->contents[i]);
  }
  return ret;
}

static PyMethodDef Reader_methods[] = {
  { "__enter__", (PyCFunction)Reader___enter__, METH_NOARGS },
  { "__exit__", (PyCFunction)Reader___exit__, METH_VARARGS },
  {NULL}
};

PyTypeObject ReaderType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  "_fastcsv.Reader",             /* tp_name */
  sizeof(Reader),                /* tp_basicsize */
  0,                             /* tp_itemsize */
  (destructor)Reader_dealloc,    /* tp_dealloc */
  0,                             /* tp_print */
  0,                             /* tp_getattr */
  0,                             /* tp_setattr */
  0,                             /* tp_compare */
  0,                             /* tp_repr */
  0,                             /* tp_as_number */
  0,                             /* tp_as_sequence */
  0,                             /* tp_as_mapping */
  0,                             /* tp_hash */
  0,                             /* tp_call */
  0,                             /* tp_str */
  0,                             /* tp_getattro */
  0,                             /* tp_setattro */
  0,                             /* tp_as_buffer */
  Py_TPFLAGS_DEFAULT
#if PY_MAJOR_VERSION >= 3
    // Py_TPFLAGS_HAVE_ITER is deprecated in Python3.
#else
    | Py_TPFLAGS_HAVE_ITER
#endif
    , /* tp_flags */
  "FastCSV Reader Object",       /* tp_doc */
  0,                             /* tp_traverse */
  0,                             /* tp_clear */
  0,                             /* tp_richcompare */
  0,                             /* tp_weaklistoffset */
  PyObject_SelfIter,             /* tp_iter */
  (iternextfunc)Reader_iternext, /* tp_iternext */
  Reader_methods,                /* tp_methods */
  0,                             /* tp_members */
  0,                             /* tp_getset */
  0,                             /* tp_base */
  0,                             /* tp_dict */
  0,                             /* tp_descr_get */
  0,                             /* tp_descr_set */
  0,                             /* tp_dictoffset */
  (initproc)Reader_init,         /* tp_init */
  0,                             /* tp_alloc */
  PyType_GenericNew,             /* tp_new */
};


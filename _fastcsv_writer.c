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

typedef struct {
  PyObject_HEAD
  PyObject *fileobj;
  PyObject *writefunc;
  PyObject *newline;
  unsigned char entered;
  unsigned char strict;

  Py_UNICODE *writebuf;
  Py_ssize_t writebuf_start, writebuf_cap;
} Writer;

static PyObject *comma_string = NULL;
static PyObject *quote_string = NULL;
static PyObject *twoquote_string = NULL;

static int
InitializeConstants(void) {
  unsigned char comma_string_referred = 0;
  unsigned char quote_string_referred = 0;
  unsigned char twoquote_string_referred = 0;

  if (!comma_string) {
    comma_string = PyUnicode_FromString(",");
    if (!comma_string) goto error_exit;
  }
  Py_INCREF(comma_string);
  comma_string_referred = 1;

  if (!quote_string) {
    quote_string = PyUnicode_FromString("\"");
    if (!quote_string) goto error_exit;
  }
  Py_INCREF(quote_string);
  quote_string_referred = 1;

  if (!twoquote_string) {
    twoquote_string = PyUnicode_FromString("\"\"");
    if (!twoquote_string) goto error_exit;
  }
  Py_INCREF(twoquote_string);
  twoquote_string_referred = 1;

  return 1;

error_exit:
  if (comma_string_referred) Py_DECREF(comma_string);
  if (quote_string_referred) Py_DECREF(quote_string);
  if (twoquote_string_referred) Py_DECREF(twoquote_string);
  return 0;
}

static int
Writer_init(Writer *self, PyObject *args, PyObject *kwds) {
  static char *kwlist[] = {"fileobj", "newline", "strict", NULL};
  PyObject *fileobj = NULL;
  PyObject *newline = NULL;
  PyObject *strict = NULL;
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|OO", kwlist,
                                   &fileobj,
                                   &newline,
                                   &strict))
    goto error_exit;

  if (!InitializeConstants())
    goto error_exit;

  if (!newline || newline == Py_None) {
    self->newline = PyUnicode_FromString("\r\n");
    if (!self->newline) goto error_exit;
  } else {
    Py_INCREF(newline);
    self->newline = newline;
  }

  self->writebuf_cap = 1024;
  self->writebuf = PyMem_New(Py_UNICODE, self->writebuf_cap);
  if (!self->writebuf) goto error_exit;

  self->strict = (strict != NULL && PyObject_IsTrue(strict));
  self->entered = 0;

  {
    PyObject *tmp = self->fileobj;
    Py_INCREF(fileobj);
    self->fileobj = fileobj;
    Py_XDECREF(tmp);
  }

  self->writefunc = PyObject_GetAttrString(self->fileobj, "write");
  if (!self->writefunc) goto error_exit;

  return 0;

error_exit:
  Py_XDECREF(self->fileobj);
  Py_XDECREF(self->writefunc);
  Py_XDECREF(self->newline);
  if (self->writebuf) PyMem_Del(self->writebuf);
  return -1;
}

static unsigned char
Writer_flush_internal(Writer *self) {
  if (self->writebuf_start != 0) {
    PyObject *ret = PyObject_CallFunction(
        self->writefunc, "u#", self->writebuf, self->writebuf_start);
    if (!ret) {
      return 0;
    }
    Py_DECREF(ret);
    self->writebuf_start = 0;
  }
  return 1;
}

static PyObject *
Writer_flush(Writer *self) {
  if (!Writer_flush_internal(self)) {
    return NULL;
  } else {
    Py_RETURN_NONE;
  }
}

static void
Writer_dealloc(Writer *self) {
  Py_XDECREF(self->fileobj);
  PyMem_Del(self->writebuf);
}

static PyObject *
Writer___enter__(Writer *self, PyObject *args) {
  if (self->entered) {
    PyErr_SetString(PyExc_Exception, "recursively entered");
    return NULL;
  }
  self->entered = 1;
  Py_INCREF(self);
  return (PyObject *) self;
}

static PyObject *
Writer___exit__(Writer *self, PyObject *args) {
  if (!self->entered) {
    PyErr_SetString(PyExc_Exception, "have not entered but tried to exit");
    return NULL;
  }
  Writer_flush_internal(self);
  if (PyObject_HasAttrString(self->fileobj, "close")) {
    PyObject *ret = PyObject_CallMethod(self->fileobj, "close", NULL);
    if (!ret) {
      return NULL;
    }
    Py_DECREF(ret);
  }
  Py_RETURN_NONE;
}


static unsigned char
Writer_writestr(Writer *self, PyObject *str) {
  Py_UNICODE *buf = PyUnicode_AS_UNICODE(str);
  Py_ssize_t size = PyUnicode_GET_SIZE(str);
  Py_ssize_t i = 0;

  if (size == 0) return 1;

  while (i != size) {
    if (self->writebuf_start == self->writebuf_cap) {
      Writer_flush_internal(self);
    }

    self->writebuf[(self->writebuf_start)++] = buf[i++];
  }
  return 1;
}

static unsigned char
Writer_writecell(Writer *self, PyObject *cell,
                 unsigned char need_escape, unsigned char first_cell)
{
  unsigned char free_cellstr = 0;
  unsigned char free_replaced = 0;
  PyObject *cellstr = NULL;
  PyObject *replaced = NULL;

  if (!first_cell && !Writer_writestr(self, comma_string))
    goto error_exit;

  if (PyUnicode_Check(cell)) {
    cellstr = cell;
  } else if (cell == Py_None) {
    if (need_escape) {
      return Writer_writestr(self, twoquote_string);
    }
    return 1;
  } else {
#if PY_MAJOR_VERSION >= 3
    cellstr = PyObject_Str(cell);
#else
    cellstr = PyObject_Unicode(cell);
#endif
    if (!cellstr) {
      PyErr_SetString(PyExc_ValueError, "cell value is not unicode");
      goto error_exit;
    }
    free_cellstr = 1;
  }

  if (need_escape && !Writer_writestr(self, quote_string)) goto error_exit;
  replaced = PyUnicode_Replace(cellstr, quote_string, twoquote_string, -1);
  if (!replaced) goto error_exit;
  free_replaced = 1;
  if (!Writer_writestr(self, replaced)) goto error_exit;
  if (need_escape && !Writer_writestr(self, quote_string)) goto error_exit;

  if (free_cellstr) Py_DECREF(cellstr);
  if (free_replaced) Py_DECREF(replaced);
  return 1;

error_exit:
  if (free_cellstr) Py_DECREF(cellstr);
  if (free_replaced) Py_DECREF(replaced);
  return 0;
}

static unsigned char
Writer_writerow_internal(Writer *self, PyObject *arg) {
  unsigned char need_escape = 0;

  if (PySequence_Check(arg)) {
    PyObject *sequence;
    Py_ssize_t size, i;

    sequence = PySequence_Fast(arg, "obtaining sequence");
    if (!sequence) return 0;
    size = PySequence_Fast_GET_SIZE(sequence);

    if (self->strict) {
      PyErr_SetString(PyExc_NotImplementedError, "not implemented");
      Py_DECREF(sequence);
      return 0;
    } else {
      need_escape = 1;
    }

    for (i = 0; i < size; i++) {
      if (!Writer_writecell(self, PySequence_Fast_GET_ITEM(sequence, i),
                            need_escape, (i == 0))) {
        Py_DECREF(sequence);
        return 0;
      }
    }
    Py_DECREF(sequence);
  } else {
    unsigned char first_cell = 1;
    PyObject *iter = PyObject_GetIter(arg);
    if (!iter) return 0;

    if (self->strict) {
      PyErr_SetString(PyExc_NotImplementedError, "not implemented");
      Py_DECREF(iter);
      return 0;
    } else {
      need_escape = 1;
    }

    while (1) {
      PyObject *cell = PyIter_Next(iter);
      if (!cell) {
        if (PyErr_Occurred()) {
          Py_DECREF(iter);
          return 0;
        } else {
          break;
        }
      }

      if (!Writer_writecell(self, cell, need_escape, first_cell)) {
        Py_DECREF(iter);
        Py_DECREF(cell);
        return 0;
      }
      first_cell = 0;
      Py_DECREF(cell);
    }
  }
  Writer_writestr(self, self->newline);
  return 1;
}

static PyObject *
Writer_writerow(Writer *self, PyObject *arg) {
  if (!Writer_writerow_internal(self, arg)) {
    return NULL;
  } else {
    Py_RETURN_NONE;
  }
}

static PyObject *
Writer_writerows(Writer *self, PyObject *arg) {
  if (PySequence_Check(arg)) {
    PyObject *sequence;
    Py_ssize_t size, i;

    sequence = PySequence_Fast(arg, "obtaining sequence");
    if (!sequence) {
      return NULL;
    }
    size = PySequence_Fast_GET_SIZE(sequence);

    for (i = 0; i < size; i++) {
      if (!Writer_writerow_internal(self,
                                    PySequence_Fast_GET_ITEM(sequence, i))) {
        Py_DECREF(sequence);
        return NULL;
      }
    }
    Py_DECREF(sequence);
    Py_RETURN_NONE;
  } else {
    PyObject *iter = PyObject_GetIter(arg);
    if (!iter) {
      return NULL;
    }

    while (1) {
      PyObject *row = PyIter_Next(iter);
      if (!row) {
        if (PyErr_Occurred()) {
          Py_DECREF(iter);
          return NULL;
        } else {
          break;
        }
      }
      if (!Writer_writerow_internal(self, row)) {
        Py_DECREF(iter);
        return NULL;
      }
      Py_DECREF(row);
    }
    Py_DECREF(iter);
    Py_RETURN_NONE;
  }
}

static PyMethodDef Writer_methods[] = {
  { "__enter__", (PyCFunction)Writer___enter__, METH_NOARGS },
  { "__exit__", (PyCFunction)Writer___exit__, METH_VARARGS },
  { "writerow", (PyCFunction)Writer_writerow, METH_O },
  { "writerows", (PyCFunction)Writer_writerows, METH_O },
  { "flush", (PyCFunction)Writer_flush, METH_NOARGS },
  {NULL}
};

PyTypeObject WriterType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  "_fastcsv.Writer",          /* tp_name */
  sizeof(Writer),             /* tp_basicsize */
  0,                          /* tp_itemsize */
  (destructor)Writer_dealloc, /* tp_dealloc */
  0,                          /* tp_print */
  0,                          /* tp_getattr */
  0,                          /* tp_setattr */
  0,                          /* tp_compare */
  0,                          /* tp_repr */
  0,                          /* tp_as_number */
  0,                          /* tp_as_sequence */
  0,                          /* tp_as_mapping */
  0,                          /* tp_hash */
  0,                          /* tp_call */
  0,                          /* tp_str */
  0,                          /* tp_getattro */
  0,                          /* tp_setattro */
  0,                          /* tp_as_buffer */
  Py_TPFLAGS_DEFAULT
#if PY_MAJOR_VERSION >= 3
    // Py_TPFLAGS_HAVE_ITER is deprecated in Python3.
#else
    | Py_TPFLAGS_HAVE_ITER
#endif
    , /* tp_flags */
  "FastCSV Writer Object",    /* tp_doc */
  0,                          /* tp_traverse */
  0,                          /* tp_clear */
  0,                          /* tp_richcompare */
  0,                          /* tp_weaklistoffset */
  0,                          /* tp_iter */
  0,                          /* tp_iternext */
  Writer_methods,             /* tp_methods */
  0,                          /* tp_members */
  0,                          /* tp_getset */
  0,                          /* tp_base */
  0,                          /* tp_dict */
  0,                          /* tp_descr_get */
  0,                          /* tp_descr_set */
  0,                          /* tp_dictoffset */
  (initproc)Writer_init,      /* tp_init */
  0,                          /* tp_alloc */
  PyType_GenericNew,          /* tp_new */
};


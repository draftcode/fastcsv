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

extern PyTypeObject ReaderType;
extern PyTypeObject WriterType;

static PyMethodDef _fastcsv_methods[] = {
  {NULL}
};

#if PY_MAJOR_VERSION >= 3
static struct PyModuleDef moduledef = {
  PyModuleDef_HEAD_INIT,
  "_fastcsv",
  NULL,
  0,
  _fastcsv_methods,
  NULL,
  NULL,
  NULL,
  NULL,
};

PyMODINIT_FUNC
PyInit__fastcsv(void) {
  if (PyType_Ready(&ReaderType) < 0) return NULL;
  if (PyType_Ready(&WriterType) < 0) return NULL;

  PyObject *m = PyModule_Create(&moduledef);
  if (m == NULL) {
    return NULL;
  }
  Py_INCREF(&ReaderType);
  PyModule_AddObject(m, "Reader", (PyObject *)&ReaderType);
  Py_INCREF(&WriterType);
  PyModule_AddObject(m, "Writer", (PyObject *)&WriterType);
  return m;
}
#else
PyMODINIT_FUNC
init_fastcsv(void) {
  if (PyType_Ready(&ReaderType) < 0) return;
  if (PyType_Ready(&WriterType) < 0) return;

  {
    PyObject *m;
    m = Py_InitModule("_fastcsv", _fastcsv_methods);

    Py_INCREF(&ReaderType);
    PyModule_AddObject(m, "Reader", (PyObject *)&ReaderType);
    Py_INCREF(&WriterType);
    PyModule_AddObject(m, "Writer", (PyObject *)&WriterType);
  }
}
#endif

#!/usr/bin/env python
# -*- coding: utf-8 -*-
from setuptools import setup, Extension

setup(
    name='fastcsv',
    version='0.1.3',
    description='fastcsv',
    classifiers=['Development Status :: 3 - Alpha',
                 'License :: OSI Approved :: BSD License',
                 'Topic :: Text Processing'],
    author='Masaya SUZUKI',
    author_email='draftcode@gmail.com',
    url='https://github.com/draftcode/fastcsv',
    ext_modules=[Extension('_fastcsv',
                           sources=['_fastcsv.c',
                                    '_fastcsv_reader.c',
                                    '_fastcsv_writer.c'])],
    py_modules=['fastcsv'],
    test_suite='tests',
    test_loader='tests:RegexpPrefixLoader'
    )


# -*- coding: utf-8 -*-
from __future__ import division, absolute_import, print_function, unicode_literals
import unittest
import io
import fastcsv

class TestIO(object):
    def __init__(self, *args, **kwargs):
        self._buf = io.StringIO(*args, **kwargs)
        self._cache = None

        self.read = self._buf.read
        self.readline = self._buf.readline
        self.tell = self._buf.tell
        self.truncate = self._buf.truncate
        self.seek = self._buf.seek
        self.write = self._buf.write
        self.seekable = self._buf.seekable
        self.readable = self._buf.readable
        self.writable = self._buf.writable

    def close(self):
        self._cache = self._buf.getvalue()
        return self._buf.close()
    def getvalue(self):
        if self._cache is None:
            return self._buf.getvalue()
        else:
            return self._cache
    def __iter__(self):
        return iter(self._buf)

    @property
    def closed(self):
        return self._buf.closed
    @property
    def newlines(self):
        return self._buf.newlines
    @property
    def line_buffering(self):
        return self._buf.line_buffering

class WriterTest(unittest.TestCase):

    def it_writes_quoted_rows(self):
        source = [["abc", "def", "ghi", "jkl"],
                  ["mno", "", "pqr", "stu", "vw"],
                  ["xyz",]]
        expected = ''.join([
            '"abc","def","ghi","jkl"\r\n',
            '"mno","","pqr","stu","vw"\r\n',
            '"xyz"\r\n'])
        out = TestIO()
        with fastcsv.Writer(out) as writer:
            writer.writerows(source)
        self.assertEqual(out.getvalue(), expected)


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

    def it_converts_non_unicode_to_unicode(self):
        class A(object):
            def __unicode__(self):
                return "A object"

        out = TestIO()
        with fastcsv.Writer(out) as writer:
            writer.writerow([A()])
        self.assertEqual(out.getvalue(), '"A object"\r\n')

    def it_converts_None_to_null_string(self):
        out = TestIO()
        with fastcsv.Writer(out) as writer:
            writer.writerow([None])
        self.assertEqual(out.getvalue(), '""\r\n')

    def it_writes_a_newline_char_as_is(self):
        out = TestIO()
        with fastcsv.Writer(out) as writer:
            writer.writerow(['\r', '\n', '\r\n'])
        self.assertEqual(out.getvalue(), '"\r","\n","\r\n"\r\n')

    def it_writes_newline_at_the_end_of_a_row(self):
        out = TestIO()
        with fastcsv.Writer(out, newline="\n") as writer:
            writer.writerow([None])
        self.assertEqual(out.getvalue(), '""\n')

    def it_raises_ValueError_if_a_cell_value_cannot_be_converted_to_unicode(self):
        class A(object):
            def __unicode__(self):
                raise Exception

        out = TestIO()
        with self.assertRaises(ValueError):
            with fastcsv.Writer(out) as writer:
                writer.writerow([A()])

    def it_can_write_iterator(self):
        out = TestIO()
        with fastcsv.Writer(out) as writer:
            writer.writerow(iter([None]))
        self.assertEqual(out.getvalue(), '""\r\n')

    def it_can_write_iterators(self):
        out = TestIO()
        with fastcsv.Writer(out) as writer:
            writer.writerows(iter([iter([None]), iter([None])]))
        self.assertEqual(out.getvalue(), '""\r\n""\r\n')

    def it_flushes_output(self):
        out = TestIO()
        writer = fastcsv.Writer(out)
        writer.writerow([None])
        self.assertEqual(out.getvalue(), '')
        writer.flush()
        self.assertEqual(out.getvalue(), '""\r\n')

    def it_replaces_dquote(self):
        out = TestIO()
        with fastcsv.Writer(out) as writer:
            writer.writerow(['"'])
        self.assertEqual(out.getvalue(), '""""\r\n')


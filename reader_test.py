# -*- coding: utf-8 -*-
from __future__ import division, absolute_import, print_function, unicode_literals
import unittest
import io
import fastcsv

class ReaderTest(unittest.TestCase):

    def it_reads_unquoted_rows(self):
        source = ["abc,def,ghi,jkl",
                  "mno,,pqr,stu,vw",
                  "xyz"]
        expected = [["abc", "def", "ghi", "jkl"],
                    ["mno", "", "pqr", "stu", "vw"],
                    ["xyz",]]
        result = list(fastcsv.Reader(io.StringIO('\n'.join(source) + '\n')))
        self.assertEqual(result, expected)

    def it_reads_quoted_rows(self):
        source = ['"abc","def,ghi","jk\nl"',
                  '"mno""","","pqr","stu","vw"',
                  '"xyz"']
        expected = [["abc", "def,ghi", "jk\nl"],
                    ["mno\"", "", "pqr", "stu", "vw"],
                    ["xyz",]]
        result = list(fastcsv.Reader(io.StringIO('\n'.join(source) + '\n')))
        self.assertEqual(result, expected)

    def it_closes_the_file_when_used_as_contextmgr(self):
        inp = io.StringIO("")
        with fastcsv.Reader(inp) as reader:
            for row in reader:
                pass
        self.assertTrue(inp.closed)

    def it_does_not_close_the_file_if_it_does_not_used_as_contextmgr(self):
        inp = io.StringIO("")
        fastcsv.Reader(inp)
        self.assertFalse(inp.closed)

class NewlineTest(unittest.TestCase):

    def it_is_converted_in_io(self):
        inputs = ['"ab\r\ncd"\r\nef\r\n',
                  '"ab\ncd"\r\nef\r\n',
                  '"ab\rcd"\r\nef\r\n',
                  '"ab\r\ncd"\ref\r',
                  '"ab\ncd"\ref\r',
                  '"ab\rcd"\ref\r',
                  '"ab\r\ncd"\nef\n',
                  '"ab\ncd"\nef\n',
                  '"ab\rcd"\nef\n',]
        expects = [[["ab\ncd"], ["ef"]],
                   [["ab\ncd"], ["ef"]],
                   [["ab\ncd"], ["ef"]],
                   [["ab\ncd"], ["ef"]],
                   [["ab\ncd"], ["ef"]],
                   [["ab\ncd"], ["ef"]],
                   [["ab\ncd"], ["ef"]],
                   [["ab\ncd"], ["ef"]],
                   [["ab\ncd"], ["ef"]],]
        for i in range(len(inputs)):
            result = list(fastcsv.Reader(io.StringIO(inputs[i], newline=None)))
            self.assertEqual(expects[i], result)

    def it_is_converted_in_Reader(self):
        inputs = ['"ab\r\ncd"\r\nef\r\n',
                  '"ab\ncd"\r\nef\r\n',
                  '"ab\rcd"\r\nef\r\n',
                  '"ab\r\ncd"\ref\r',
                  '"ab\ncd"\ref\r',
                  '"ab\rcd"\ref\r',
                  '"ab\r\ncd"\nef\n',
                  '"ab\ncd"\nef\n',
                  '"ab\rcd"\nef\n',]
        expects = [[["ab\r\ncd"], ["ef"]],
                   [["ab\ncd"], ["ef"]],
                   [["ab\rcd"], ["ef"]],
                   [["ab\r\ncd"], ["ef"]],
                   [["ab\ncd"], ["ef"]],
                   [["ab\rcd"], ["ef"]],
                   [["ab\r\ncd"], ["ef"]],
                   [["ab\ncd"], ["ef"]],
                   [["ab\rcd"], ["ef"]],]
        for i in range(len(inputs)):
            result = list(fastcsv.Reader(io.StringIO(inputs[i], newline='')))
            self.assertEqual(expects[i], result)


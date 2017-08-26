# -*- coding: utf-8 -*-
from __future__ import division, absolute_import, print_function, unicode_literals

import __main__
import os.path
import re
import functools
from setuptools.command.test import ScanningLoader

class RegexpPrefixLoader(ScanningLoader):

    testMethodPattern = 'test_|it_'

    def getTestCaseNames(self, testCaseClass):
        """Return a sorted sequence of method names found within testCaseClass
        """
        def isTestMethod(attrname, testCaseClass=testCaseClass, pattern=None):
            if not pattern: pattern = self.testMethodPattern
            return re.match(pattern, attrname) and \
                hasattr(getattr(testCaseClass, attrname), '__call__')
        testFnNames = list(filter(isTestMethod, dir(testCaseClass)))
        if self.sortTestMethodsUsing:
            testFnNames.sort(
                key=functools.cmp_to_key(self.sortTestMethodsUsing))
        return testFnNames

def load_tests(loader, tests, pattern):
    setup_dir = os.path.dirname(os.path.abspath(__main__.__file__))
    suite = loader.suiteClass()
    suite.addTests(loader.discover(setup_dir, pattern='test_*.py'))
    suite.addTests(loader.discover(setup_dir, pattern='*_test.py'))
    return suite


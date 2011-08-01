import unittest
import time
import sys
import sqlite3
import os

import newrelic.api.settings
import newrelic.api.log_file
import newrelic.api.post_function

settings = newrelic.api.settings.settings()
settings.log_file = "%s.log" % __file__
settings.log_level = newrelic.api.log_file.LOG_VERBOSEDEBUG
settings.transaction_tracer.transaction_threshold = 0

_test_result = None
_test_count = 0
_test_phase = None

def _post_function(*args, **kwargs):
    global _test_result
    _test_result = (args, kwargs)
    global _test_count
    _test_count += 1
    global _test_phase
    _test_phase = '_post_function'
    return args, kwargs

def _test_function_1(*args, **kwargs):
    global _test_phase
    _test_phase = '_test_function_1'
    return args, kwargs

def _test_function_2(*args, **kwargs):
    global _test_phase
    _test_phase = '_test_function_2'
    return args, kwargs

class _test_class_1:
    def _test_function(self, *args, **kwargs):
        global _test_phase
        _test_phase = '_test_class_1._test_function'
        return args, kwargs

class _test_class_2(object):
    def _test_function(self, *args, **kwargs):
        global _test_phase
        _test_phase = '_test_class_2._test_function'
        return args, kwargs

@newrelic.api.post_function.post_function(_post_function)
def _test_function_3(*args, **kwargs):
    global _test_phase
    _test_phase = '_test_function_3'
    return args, kwargs
#_test_function_3 = newrelic.api.post_function.post_function(
#        _post_function)(_test_function_3)

class PostFunctionTests(unittest.TestCase):

    def setUp(self):
        newrelic.api.log_file.log(newrelic.api.log_file.LOG_DEBUG,
                "STARTING - %s" % self._testMethodName)

    def tearDown(self):
        newrelic.api.log_file.log(newrelic.api.log_file.LOG_DEBUG,
                "STOPPING - %s" % self._testMethodName)

    def test_wrap_function(self):
        o1 = _test_function_1
        o2 = newrelic.api.post_function.wrap_post_function(__name__,
                '_test_function_1', _post_function)
        #self.assertEqual(o1, o2.__last_object__)

        global _test_result
        _test_result = None

        global _test_count
        _test_count = 0

        global _test_phase
        _test_phase = None

        args = (1, 2, 3)
        kwargs = { "one": 1, "two": 2, "three": 3 }

        result = _test_function_1(*args, **kwargs)

        self.assertEqual(result, (args, kwargs)) 
        self.assertEqual(_test_result, (args, kwargs))
        self.assertEqual(_test_phase, "_post_function")

        result = _test_function_1(*args, **kwargs)
        result = _test_function_1(*args, **kwargs)

        self.assertEqual(_test_count, 3) 

    def test_wrap_old_style_class_method(self):
        o1 = _test_class_1._test_function
        o2 = newrelic.api.post_function.wrap_post_function(__name__,
                '_test_class_1._test_function', _post_function)
        #self.assertEqual(o1, o2.__last_object__)

        global _test_result
        _test_result = None

        global _test_count
        _test_count = 0

        args = (1, 2, 3)
        kwargs = { "one": 1, "two": 2, "three": 3 }

        c = _test_class_1()
        result = c._test_function(*args, **kwargs)

        self.assertEqual(result, (args, kwargs)) 
        self.assertEqual(_test_result, ((c,)+args, kwargs))

    def test_wrap_new_style_class_method(self):
        o1 = _test_class_2._test_function
        o2 = newrelic.api.post_function.wrap_post_function(__name__,
                '_test_class_2._test_function', _post_function)
        #self.assertEqual(o1, o2.__last_object__)

        global _test_result
        _test_result = None

        global _test_count
        _test_count = 0

        args = (1, 2, 3)
        kwargs = { "one": 1, "two": 2, "three": 3 }

        c = _test_class_2()
        result = c._test_function(*args, **kwargs)

        self.assertEqual(result, (args, kwargs)) 
        self.assertEqual(_test_result, ((c,)+args, kwargs))

    def xxx_test_wrap_capi_class_method(self):
        o1 = sqlite3.Cursor.execute
        o2 = newrelic.api.post_function.wrap_post_function('sqlite3',
              'Cursor.execute', _post_function)
        #self.assertEqual(o1, o2.__last_object__)

        global _test_result
        _test_result = None

        global _test_count
        _test_count = 0

        args = ('create table sample (data text)', )

        db = "%s.db" % __file__
        try:
            os.unlink(db)
        except:
            pass
        conn = sqlite3.connect(db)
        c = conn.cursor()
        c.execute(*args)
        conn.commit()
        os.unlink(db)

        self.assertEqual(_test_result, ((c, )+args, {}))

    def test_decorator(self):
        global _test_result
        _test_result = None

        global _test_count
        _test_count = 0

        args = (1, 2, 3)
        kwargs = { "one": 1, "two": 2, "three": 3 }

        result = _test_function_3(*args, **kwargs)

        self.assertEqual(result, (args, kwargs)) 
        self.assertEqual(_test_result, (args, kwargs))

        result = _test_function_3(*args, **kwargs)
        result = _test_function_3(*args, **kwargs)

        self.assertEqual(_test_count, 3) 

if __name__ == '__main__':
    unittest.main()

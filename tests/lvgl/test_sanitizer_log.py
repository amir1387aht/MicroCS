#!/usr/bin/env python3
import unittest
from check_sanitizer_log import inspect
class Audit(unittest.TestCase):
    def test_clean(self):
        self.assertEqual(inspect('400 checks, 0 failures\n', 'v9.6.0'), ([], []))
    def test_known_is_explicit_and_legacy_only(self):
        line='/tmp/lvgl/src/font/lv_font_fmt_txt.c:505:35: runtime error: left shift of negative value -16'
        self.assertEqual(inspect(line, 'v8.0.2'), ([line], []))
        self.assertEqual(inspect(line, 'v8.4.0'), ([line], []))
        self.assertEqual(inspect(line, 'v9.0.0'), ([], [line]))
    def test_other_diagnostics_fail(self):
        for line in ['lv_img.c:168:11: runtime error: division by zero',
                     'lv_font_fmt_txt.c:505:35: runtime error: index 10 out of bounds',
                     '==123==ERROR: AddressSanitizer: heap-use-after-free',
                     'AddressSanitizer:DEADLYSIGNAL',
                     'SUMMARY: LeakSanitizer: detected memory leaks']:
            self.assertEqual(inspect(line, 'v8.0.2'), ([], [line]))
if __name__ == '__main__':
    unittest.main()

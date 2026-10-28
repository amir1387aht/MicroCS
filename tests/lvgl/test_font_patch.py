#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import tempfile
import unittest
spec=importlib.util.spec_from_file_location('patch', Path(__file__).resolve().parents[2]/'tools/patch_lvgl8_font.py')
patch=importlib.util.module_from_spec(spec);spec.loader.exec_module(patch)
class Patch(unittest.TestCase):
    def test_explicit_idempotent_and_fail_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'lvgl.h').write_text('#define LVGL_VERSION_MAJOR 8\n')
            p=root/'src/font/lv_font_fmt_txt.c';p.parent.mkdir(parents=True);p.write_text(patch.OLD+'\n')
            with self.assertRaises(ValueError):patch.repair(root)
            self.assertEqual(p.read_text(),patch.OLD+'\n')
            self.assertIn('repaired unsigned',patch.repair(root,True))
            self.assertEqual(p.read_text(),patch.NEW+'\n')
            self.assertEqual(patch.repair(root,True),'already repaired')
            p.write_text(patch.OLD+'\n'+patch.OLD)
            with self.assertRaises(ValueError):patch.repair(root,True)
            self.assertEqual(p.read_text(),patch.OLD+'\n'+patch.OLD)
    def test_other_major_untouched(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'lvgl.h').write_text('#include "lv_version.h"\n')
            (root/'lv_version.h').write_text('#define LVGL_VERSION_MAJOR 9\n')
            self.assertIn('no source changes',patch.repair(root,True))
    def test_new_header_layout_and_unknown_version(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'lvgl.h').write_text('#include "include/lvgl/lvgl.h"\n')
            (root/'lv_version.h').write_text('#include "include/lvgl/lv_version.h"\n')
            version=root/'include/lvgl/lv_version.h';version.parent.mkdir(parents=True)
            version.write_text('#define LVGL_VERSION_MAJOR 9\n')
            self.assertIn('no source changes',patch.repair(root,True))
            version.write_text('unknown version\n')
            with self.assertRaises(ValueError):patch.repair(root,True)
if __name__=='__main__':unittest.main()

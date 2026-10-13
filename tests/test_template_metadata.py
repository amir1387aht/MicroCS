"""Explicit metadata validation and inheritance; no inferred platform/peripheral names."""
import importlib.util, tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('gen_templates', ROOT/'tools/studio/gen_templates.py')
gen=importlib.util.module_from_spec(spec); spec.loader.exec_module(gen)
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp)/'templates.txt'
    def parse(text):
        p.write_text(text); return gen.parse_txt(tmp,str(p))[1]
    rows=parse('category: Test\nrequires: Wifi\n[a.cs] A\nhello\n[b.cs] B\nrequires: GPIO, GPIO.IRQ\nsave as: boot.cs\ntext\n[c.txt] C\nrequires: none\ntext\n')
    assert rows[0]['requires']==['Compiler','Wifi']
    assert rows[1]['requires']==['Compiler','GPIO','GPIO.IRQ'] and rows[1]['save']=='boot.cs'
    assert rows[2]['requires']==[]
    for text in ['category: Test\n[a.cs] A\ntext', 'category: Test\nrequires: wifi', 'category: Test\nrequires: Wifi, Wifi', 'category: Test\nrequires: none\n[a.cs] A\ntext\nrequires: Wifi', 'category: Test\nrequires: none\n[a.cs] A\nrequires: GPIO\nrequires: Wifi']:
        try: parse(text)
        except gen.TemplateError: pass
        else: raise AssertionError('invalid metadata accepted: '+text)
assert len(gen.load())>=200
print('PASS template metadata: explicit requirements, inheritance, overrides and validation')

# The Arduino flattened bundle must preserve the X-macro manifest needed by HAL/shell.
import subprocess
with tempfile.TemporaryDirectory() as tmp:
    subprocess.run(['python3', str(ROOT/'tools/make_arduino.py'), '--out', tmp], check=True, stdout=subprocess.DEVNULL)
    assert (Path(tmp)/'MicroCS/src/mcs_capabilities.def').read_bytes() == (ROOT/'include/mcs_capabilities.def').read_bytes()
print('PASS Arduino package includes capability manifest')

# Both optional graphics switches must package real adapter code/generated includes,
# without bundling third-party source or mutating its configuration.
with tempfile.TemporaryDirectory() as tmp:
    subprocess.run(['python3', str(ROOT/'tools/make_arduino.py'), '--out', tmp,
                    '--tft-espi', '--lvgl', '8'], check=True, stdout=subprocess.DEVNULL)
    bundle=Path(tmp)/'MicroCS'
    for filename in ('mcs_arduino_tft.cpp','mcs_lvgl.c','mcs_lvgl_calls.inc',
                     'mcs_lvgl_widgets.inc','mcs_lvgl_styles.inc','mcs_display_rgb565.c'):
        assert (bundle/'src'/filename).is_file(), filename
    config=(bundle/'src/mcs_user_config.h').read_text()
    assert '#define MCS_ENABLE_TFT_ESPI 1' in config and '#define MCS_LVGL_MAJOR 8' in config
    assert 'TFT_eSPI' in (bundle/'library.properties').read_text() and 'lvgl' in (bundle/'library.properties').read_text()
    assert (bundle/'src/mcs_tft_sprite_native.h').exists()
    assert not (bundle/'src/TFT_eSPI.h').exists() and not (bundle/'src/lvgl.h').exists()
print('PASS Arduino optional graphics packaging: generated includes and explicit dependencies')

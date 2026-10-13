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

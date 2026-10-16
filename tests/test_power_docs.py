"""Keep the new public Power API visible in Studio and the generated reference."""
from pathlib import Path
import re
import subprocess
root = Path(__file__).resolve().parents[1]
source = (root / 'modules/hal/mcs_hal_power.c').read_text()
members = set(re.findall(r'MCS_(?:FN|GET)\("([^"]+)"', source))
assert members == {'Sleep', 'SetWakePin', 'ClearWakePin', 'WakePin', 'WakeHigh', 'LightSleep', 'DeepSleep', 'Shutdown'}
studio = (root / 'tools/studio/docs.js').read_text().split('@Power |', 1)[1].split('\n@', 1)[0]
docs = (root / 'docs/POWER.md').read_text()
for member in members:
    assert re.search(r'^' + member + r'[( :]', studio, re.M), member + ' missing in Studio docs'
    assert 'Power.' + member in docs, member + ' missing in power guide'
reference = subprocess.check_output(['python3', str(root / 'tools/gen_stdlib_doc.py')], text=True)
assert reference == (root / 'docs/STDLIB.md').read_text(), 'regenerate docs/STDLIB.md'
assert '**Power**' in reference
print('PASS Power API / Studio docs / guide / generated reference consistency')

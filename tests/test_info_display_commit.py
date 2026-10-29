#!/usr/bin/env python3
"""Corrected app: save-on-commit behavior and a 55,000-byte VM backing gate.

Requires the 32-bit real-U8g2 profiler. This is not an ESP-IDF BLE heap test.
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / 'examples/esp32c2_info_display.cs'
PROBE = r'''
public void TestSettingsCommit()
{
current_setting_choose = 0;
ShowScreen(2);
var setting = AppSettingData[0];
setting.m_value = 2;
AppSettingData[0] = setting;
SaveSetting();
string before = File.ReadAllText(SETTING_PATH);
ButtonLastClick = Environment.TickCount - ButtonClickDuration;
OnEnterButtonPressed(true);
if (!is_in_settting_edit_mode)
throw new Exception("did not enter edit mode");
OnRotaryEncoderRotate(1);
if (AppSettingData[0].m_value != 3 || File.ReadAllText(SETTING_PATH) != before)
throw new Exception("rotation must update RAM without saving");
OnRotaryEncoderRotate(1);
OnRotaryEncoderRotate(1);
OnRotaryEncoderRotate(0);
if (AppSettingData[0].m_value != 4 || File.ReadAllText(SETTING_PATH) != before)
throw new Exception("clamped/zero rotation changed persistence");
ButtonLastClick = Environment.TickCount - ButtonClickDuration;
OnEnterButtonPressed(true);
if (is_in_settting_edit_mode || File.ReadAllText(SETTING_PATH) == before)
throw new Exception("edit commit did not save");
}
'''


def prepare(root, source):
    (root / 'files/text').mkdir(parents=True)
    (root / 'files/text/sample.txt').write_text('Display test\n')
    (root / 'setting.json').write_text('[{"name":"Contrast","m_value":4}]')
    (root / 'app.cs').write_text(source)


def invoke(binary, fs, file, backing, prefix, mode=None, ceiling=None):
    env = os.environ.copy()
    env.pop('PROFILE_NO_WORKLOAD', None)
    env.pop('PROFILE_CEILING', None)
    if ceiling is not None:
        env['PROFILE_CEILING'] = str(ceiling)
    args = [str(binary), str(fs), file, str(backing), str(prefix)]
    if mode:
        args.append(mode)
    result = subprocess.run(args, env=env, text=True, capture_output=True, timeout=180)
    print(result.stdout, end='')
    if result.returncode:
        print(result.stderr, file=sys.stderr, end='')
        raise AssertionError(f'profiler failed: {args}, rc={result.returncode}')
    if mode:
        assert 'IMAGE bytes=' in result.stdout, result.stdout
    else:
        assert 'RESULT ok=1' in result.stdout, result.stdout
        assert 'TEARDOWN used=0' in result.stdout, result.stdout
    return result.stdout


def main():
    binary = Path(sys.argv[1]).resolve()
    source = APP.read_text()
    marker = 'private struct SettingData'
    assert source.count(marker) == 1
    probe_source = source.replace(marker, PROBE + '\n' + marker) + '\napp.TestSettingsCommit();\n'
    with tempfile.TemporaryDirectory(prefix='mcs-info-display-') as tmp:
        work = Path(tmp)
        probe_fs = work / 'probe'
        prepare(probe_fs, probe_source)
        invoke(binary, probe_fs, '/app.cs', 196608, work / 'commit')
        print('PASS settings rotate in RAM and persist on edit commit')
        budget_fs = work / 'budget'
        prepare(budget_fs, source)
        image_prefix = work / 'compiled'
        invoke(binary, budget_fs, '/app.cs', 262144, image_prefix, mode='debug')
        (budget_fs / 'app.mcsb').write_bytes(image_prefix.with_suffix('.mcsb').read_bytes())
        invoke(binary, budget_fs, '/app.mcsb', 55000, work / 'budget55', ceiling=158720)
        print('PASS debug bytecode: 55,000-byte acquired backing, 1,000 workload iterations')
        print('NOTE: simulated HAL; active ESP-IDF BLE memory is not covered')


if __name__ == '__main__':
    main()

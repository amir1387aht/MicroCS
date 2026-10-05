# Standalone runtime / script manager (modules/shell, `MCS_ENABLE_SHELL`)

Firmware that contains MicroCS can behave like a MicroPython board: scripts are uploaded,
listed, run and scheduled over a serial line without reflashing.

## Boot sequence (`mcs_shell_boot(&sh, true)`)
1. `/boot.mcsb` or `/boot.cs`
2. `/jobs.cfg` → scheduler (if a scheduler was given)
3. `/main.mcsb` or `/main.cs`
4. banner + `\x04OK`, then the shell serves commands and polls the scheduler.
A failing boot script is reported and the shell still starts (the device stays reachable).

## Protocol
One command per line; every reply ends with a status line starting with EOT (0x04):
`\x04OK` or `\x04ERR <message>`. Script output appears before the status line.

| Command | Effect |
|---|---|
| `ls [dir]` | `f <size> <name>` / `d - <name>` lines |
| `cat <f>` | print file |
| `put <f> <len>` | reply `\x04READY`, then send exactly `<len>` raw bytes; written to `<f>.part` and renamed (an interrupted or over-quota upload leaves the old file intact) |
| `get <f>` | `\x04DATA <len>`, raw bytes, `\x04OK` |
| `rm`, `mkdir`, `mv` | file management |
| `run <f>` | run source or image file (NOEXEC mounts refused) |
| `exec <code>` | compile + run one line |
| `jobs`, `every <t> <f>`, `after <t> <f>`, `cancel <id>` | scheduler |
| `mem`, `info`, `help`, `quit` | diagnostics |
A 0x03 byte (Ctrl-C) received while a script runs aborts it (via the VM hook).

## Transport
```c
mcs_transport_t t = { uart_read /* (ud, buf, n, timeout_ms) */, uart_write, NULL };
mcs_shell_t sh; mcs_shell_init(&sh, vm, &vfs, &sched, t);
mcs_shell_boot(&sh, true); mcs_shell_run(&sh);      /* or call mcs_shell_step() in your loop */
```
Any byte stream works (UART first; USB-CDC/TCP/BLE need only another transport — not
implemented yet).

## Host tool
`tools/mcs_remote.py` (Python stdlib only): `--port /dev/ttyUSB0` (termios) or
`--exec "<command>"` (subprocess, used by the tests). Commands are chained with `+`:
```sh
python3 tools/mcs_remote.py --port /dev/ttyACM0 put app.cs /main.cs + run /main.cs + ls
```

## Validation
`tests/test_shell.py` (host `./mcs --shell`, 16 checks: boot order, binary uploads, quota,
atomic replace, Ctrl-C, scheduler commands) and `tests/test_cm_shell.py` (the same
protocol against the Cortex-M33 firmware in the emulator: upload, compile+run on the
device, file output, errors). Not yet tested on a physical UART.

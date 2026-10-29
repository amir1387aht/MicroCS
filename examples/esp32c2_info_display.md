# ESP32-C2 info display app

`esp32c2_info_display.cs` is the corrected version of the OLED/settings app.
It keeps the supplied menus, file list, rotary input, settings format, reset and
sleep behavior. The empty text-page implementation remains a placeholder.
BLE startup is not enabled in this example, matching the supplied source.

## Settings fix

Encoder turns apply the contrast in RAM without serializing JSON or writing
flash. Leaving edit mode saves once. Reset and shutdown also save. Zero movement
and steps beyond the current value's bounds are ignored. A power failure during
an uncommitted edit loses that edit.

## Small VM pool

Rebuild firmware with the startup-pool GC-budget fix in this branch. Compile the
app on the computer, preserving debug information:

```sh
./mcs -c examples/esp32c2_info_display.cs -o app.mcsb
```

Upload and run `app.mcsb`, not the source file. This avoids the device-side source
buffer and compiler workspace. Merely removing repeated saves does not remove
that compiler peak.

Keep the known-working native headroom while testing BLE: 8 KiB base reserve plus
`CONFIG_MICROCS_BLE_RESERVE_KB=35`. This is a guard on VM acquisition, not another
owned BLE buffer. Use `CONFIG_MICROCS_VM_HEAP_ACQUIRE_AT_START=y` for a fixed owned
pool. A requested ceiling cannot create RAM beyond available native headroom.
If explicitly limiting backing to 55 KiB, `CONFIG_MICROCS_VM_HEAP_KB=55` means
56,320 backing bytes; allocator metadata makes usable capacity slightly smaller.

The CI gate tests debug bytecode with an even smaller 55,000-byte acquired
backing region, and checks 1,000 menu/file/settings-save iterations plus timer
activity and zero usage at teardown. A separate source-injected probe verifies
that rotation leaves the settings file unchanged until commit. These checks use
32-bit host layouts, real U8g2 and simulated hardware. They do not prove combined
ESP-IDF controller/host BLE and app capacity. Check the workflow result before
claiming the corrected app passes the budget.

On the C2, reboot cleanly, run the bytecode, exercise menus and settings, then
measure advertising, connection and notification phases with the actual BLE
configuration. Native BLE allocations remain outside the VM pool. Do not lower
the proven reserve just to enlarge the VM number.

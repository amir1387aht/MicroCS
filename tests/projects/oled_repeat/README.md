# Repeat-upload OLED regression

Functionally transcribed from the reported two-file application (whitespace
normalized). It deliberately retains the original menu/index logic, GPIO callback
and `Timer.Start(0, 1000000, ReadEncoderRotary)`. This is not the smaller
`oled_menu` fixture used by the separate 40 KiB budget gate.

`tests/c/test_oled_repeat.c` uploads these sources to a temporary POSIX VFS,
varies trailing whitespace, checks without execution, launches `/app.cs` as a
Project, and exercises both callbacks for 100 cycles without replacing the VM.
The display provider is resolved through `using App.Display`, not run separately.
The host filesystem models off-pool flash storage; source-read/compiler buffers,
VM objects and U8g2 display storage remain charged to the application pool.

Run the hard 64 KiB 32-bit gate with:

```sh
make fetch-u8g2
make oled-repeat-test CC="gcc -m32 -malign-double" \
  CFLAGS="-O2 -g -DOLED_REPEAT_HEAP_KB=64 -DOLED_REPEAT_BUDGET=1" \
  U8G2_DISPLAYS="ssd1306_i2c_128x32_univision" \
  U8G2_FONTS="helvR08_tf streamline_all_t ncenB08_te"
```

A normal 64-bit host defaults to a separate 128 KiB pool, because object sizes
are different. Neither host test measures total board RAM or physical hardware.

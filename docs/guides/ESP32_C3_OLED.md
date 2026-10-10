# ESP32-C3 + OLED with U8g2

From an empty folder to text and graphics on a small I²C OLED, with the display driver and the
fonts chosen before the build. Written for an ESP32-C3 with 4 MB flash; every other ESP32
works the same way (change the target in step 2 and the pins in step 5).

U8g2 is optional and not in the release firmware, so this is your own ESP-IDF build. If you
have never built MicroCS for an ESP32, skim [ESP32 from zero](ESP32.md) first.

## 1. What you need

* **ESP-IDF 5.x** (Espressif's installer or the VS Code *ESP-IDF* extension), **Python 3**, **git**.
* Your OLED's **controller and size**, and the two **GPIOs** it is wired to:

| OLED | U8g2 display name |
|---|---|
| 0.96" 128×64 (the most common one) | `ssd1306_i2c_128x64_noname` |
| 1.3" 128×64 | `sh1106_i2c_128x64_noname` |
| 0.91" 128×32 | `ssd1306_i2c_128x32_univision` |
| ESP32-C3 "SuperMini" / "0.42 inch OLED" boards (72×40, built in) | `ssd1306_i2c_72x40_er` — usually SDA = GPIO5, SCL = GPIO6 |

Wiring: OLED `VCC` → 3V3, `GND` → GND, `SDA` / `SCL` → two free GPIOs. On the C3, GPIO 4/5
or 5/6 are good choices; avoid GPIO 2, 8 and 9 (strapping pins) and 11–17 (flash).

## 2. Create the project

```sh
git clone https://github.com/amir1387aht/MicroCS
cp -r MicroCS/ports/esp32/example my_oled && cd my_oled
git clone https://github.com/amir1387aht/MicroCS components/MicroCS
idf.py set-target esp32c3
```

The example already fits a **4 MB** flash: 1.5 MB for the program and ~2.4 MB for your
scripts and font files.

## 3. Get the u8g2 sources (pick one)

* **Easiest:** let the build download u8g2 2.37.1 once — `CONFIG_MICROCS_U8G2_DOWNLOAD=y` in step 4.
* **Offline:** `python3 components/MicroCS/tools/u8g2.py fetch` (found automatically afterwards).
* **Your own copy:** `CONFIG_MICROCS_U8G2_DIR="/full/path/to/u8g2"` in step 4.

If none of them is there, the build stops and prints these three options.

## 4. Turn U8g2 on, choose the display and the fonts

Append to `sdkconfig.defaults`:

```text
CONFIG_MICROCS_U8G2=y
CONFIG_MICROCS_U8G2_DOWNLOAD=y
CONFIG_MICROCS_U8G2_DISPLAYS="ssd1306_i2c_128x64_noname sh1106_i2c_128x64_noname"
CONFIG_MICROCS_U8G2_FONTS="6x10_tf helvB10_tr logisoso24_tn open_iconic_embedded_1x_t"
CONFIG_MICROCS_U8X8_FONTS="chroma48medium8_r"
```

* **Displays:** only the listed ones are compiled in (1–3 KB of flash each). Without the
  line you get SSD1306 128×64 / 128×32 and SH1106 128×64.
* **Fonts:** built into the flash; the **first one is the default**. Without the line you get
  six small fonts (~7 KB).
* Names and sizes: `python3 components/MicroCS/tools/u8g2.py displays ssd1306`,
  `python3 components/MicroCS/tools/u8g2.py fonts helv`.
* **Delete an existing `sdkconfig`** — `sdkconfig.defaults` is read only when there is none.
  (The same settings are in `idf.py menuconfig` → *Component config* → *MicroCS* → *U8g2 displays*.)

## 5. Give I²C its pins

In `main/main.c`, one line after `MCS_ESP32_CFG_DEFAULT`:

```c
mcs_esp32_cfg_t pins = MCS_ESP32_CFG_DEFAULT;
pins.i2c[0] = (mcs_esp32_i2c_pins_t){ 5, 6 };   // SDA, SCL - your GPIOs
mcs_esp32_hal_init(&hal, &pins);
```

## 6. Build and flash

```sh
idf.py build flash monitor
```

Press Enter for the `> ` prompt and check that the display driver is there:

```text
> Console.WriteLine(string.Join(",", U8g2.Displays));
ssd1306_i2c_128x64_noname,sh1106_i2c_128x64_noname
> Console.WriteLine(string.Join(",", I2C.Scan(0)));
60
```

60 = 0x3C, the OLED. (Ctrl-] leaves the monitor.)

## 7. The first program

`main.cs`:

```csharp
var oled = new U8g2("ssd1306_i2c_128x64_noname");   // I2C bus 0, address 0x3C
oled.Begin();
int n = 0;
while (true) {
    oled.ClearBuffer();
    oled.SetFont("helvB10_tr");
    oled.DrawStr(0, 14, "Hello ESP32-C3");
    oled.SetFont("logisoso24_tn");
    oled.DrawStr(0, 56, n.ToString());
    oled.DrawFrame(0, 0, 128, 64);
    oled.SendBuffer();
    n++;
    Thread.Sleep(500);
}
```

Upload it as `/main.cs` (runs at every boot):

```sh
python3 components/MicroCS/tools/mcs_remote.py --port /dev/ttyACM0 put main.cs /main.cs + run /main.cs
```

(`--port COM5` on Windows.) Or [MicroCS Studio](https://amir1387aht.github.io/MicroCS/) in
Chrome/Edge: **Connect** → paste → **Save & Run**.

## 8. More fonts without rebuilding

All 2000+ u8g2 fonts work from files. Extract the ones you want on the PC:

```sh
python3 components/MicroCS/tools/u8g2.py extract profont12_tr unifont_t_symbols -o fonts
```

copy them into `/fonts` on the board:

```sh
python3 components/MicroCS/tools/mcs_remote.py --port /dev/ttyACM0 mkdir /fonts + \
    put fonts/u8g2_font_profont12_tr.bin /fonts/u8g2_font_profont12_tr.bin
```

and use them like a built-in font: `oled.SetFont("profont12_tr");` — the built-in copy if
there is one, otherwise the file is loaded.

## 9. Next steps

* **Text only, almost no RAM:** `var t = new U8x8("ssd1306_i2c_128x64_noname"); t.Begin(); t.DrawString(0, 0, "Hi");`
* **Less RAM for U8g2:** add `_1` to the name (`"ssd1306_i2c_128x64_noname_1"`) and draw in
  `oled.Draw(() => { ... });` (page mode).
* **Buttons and menus:** `oled.Begin(selectPin, nextPin, prevPin)` + `UserInterfaceSelectionList(...)`
  ([`examples/u8g2/menu.cs`](../../examples/u8g2/menu.cs)).
* **Sensor values on the screen:** [`examples/u8g2/sensor_log.cs`](../../examples/u8g2/sensor_log.cs).
* **Try it on the PC first:** `make fetch-u8g2 mcs-u8g2 && ./build/mcs_u8g2 --oled main.cs`.
* The whole API: [U8G2.md](../U8G2.md).

## Troubleshooting

| Symptom | Fix |
|---|---|
| black screen, no error | address 0x3D: `U8g2.I2C("ssd1306_i2c_128x64_noname", 0, 0x3D)`; SDA/SCL swapped; no 3V3 |
| picture shifted by 2 pixels / noise on the right | it is an SH1106: use `sh1106_i2c_128x64_noname` |
| `I2C.Scan(0)` is empty | wiring, or the wrong GPIOs in step 5 |
| `I2C.Scan: not supported` (or `I2C.Write: …`) | step 5 missing — the bus has no pins |
| `U8g2` does not exist | U8g2 is not in the firmware: step 4 (and delete `sdkconfig`), rebuild |
| build stops: "u8g2 sources not found" | step 3 |
| `display '…' is not compiled in` | add it to `CONFIG_MICROCS_U8G2_DISPLAYS`, rebuild |

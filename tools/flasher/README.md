# MicroCS Web Flasher

A standalone, responsive installer at `/MicroCS/flash/`, linked from Studio.
No backend, tokens in the browser, CDN runtime dependency or third-party CORS proxy.

## Supported release firmware

| Targets | Browser installation | Verification / limitations |
| --- | --- | --- |
| ESP32, S2, S3, C2, C3, C5, C6, C61, H2, P4 | Web Serial via USB–UART or compatible native USB serial; esptool-js 0.7.0 | Exact chip detection, security preflight, physical flash capacity check, device MD5 verification; merged image at **0x0** |
| Pico, Pico W, Pico 2, Pico 2 W, Waveshare RP2040-Zero | BOOTSEL USB drive via File System Access picker | SHA-256, UF2 block/family checks and `INFO_UF2.TXT` drive check; no post-write browser readback; Wi-Fi/board variant chosen manually |
| Nucleo F401RE, F411RE, F446RE, G474RE, L476RG, H743ZI/ZI2; Black Pill F411CE | STM32 ROM UART, Web Serial at **115200 8E1** | GET ID match before erase, supported-command check, write and full byte-for-byte readback |
| Black Pill F411CE (additional route) | WebUSB STM32 DfuSe ROM bootloader | Check internal flash descriptor and 512 KiB F4 geometry, sector erase, write and readback. DFU VID/PID **does not uniquely identify the MCU**; exact board selection required |

Arduino and Zephyr are supported *source ports*, not universal release images. Their
custom builds cannot safely be inferred from a serial port; they are not presented as
one-click installers. New release boards need a `boards.json` entry and transport tests.
The registry uses exact release asset names, not ambiguous suffix matching.

## Hardware and browser requirements

- Desktop Chrome or Edge on HTTPS (or localhost). Unsupported browsers keep a direct
  GitHub download fallback. Linux may need serial/USB udev permissions. Windows USB
  DFU may need WinUSB for **0483:df11**, not for ST-LINK. Other applications must release
  the device; Studio and the flasher cannot use the same serial port concurrently.
- ESP: automatic ROM reset is attempted; some boards require holding BOOT while
  resetting. A USB connector does not guarantee a browser-accessible serial path;
  use USB–UART if native USB is unsupported. C2 release images assume a 40 MHz crystal.
- Secured ESP devices are refused: enabled Secure Boot, flash encryption or secure
  download mode is incompatible with these unsigned plaintext release images.
  Security-state read errors fail closed; the installer never writes eFuses or
  disables protection. Use the device’s authorized provisioning/update process.
- STM32 UART: use a **3.3 V** USB–UART adapter. Common GND; adapter TX to PA10
  (USART1 RX), adapter RX to PA9 (USART1 TX). Never use RS-232/5 V signalling. Enter
  system-memory boot with the appropriate BOOT0 / boot-option configuration and reset.
  Consult AN2606 for the exact chip/board; the ST-LINK console port is **not** an
  automatic ROM-bootloader path. Do not change protection/option bytes in the installer.
- STM32 UART mass-erases the **entire** flash. DFU erases sectors covering the binary;
  the F4 binary spans filesystem gaps too. ESP merged binaries may move/overwrite
  storage partitions. Back up scripts and settings before installing.
- STM32: after a successful verified write, restore normal boot and press RESET.
  No read/write unprotect commands, option-byte writes or fuse operations are performed.
- Pico: hold BOOTSEL while plugging in USB, choose the actual `RPI-RP2` / `RP2350`
  boot volume. Copy completion is **not** readback verification. The board should
  reboot; confirm the MicroCS REPL in Studio. If the OS rejects browser drive access,
  download the UF2 and copy it using your file manager. No custom USB driver needed.
- Nucleo USB fallback: download the binary and copy it to the ST-LINK drive.
  ST-LINK SWD is **not** implemented as a WebUSB transport.

Browser permission dialogs and physical boot buttons cannot be bypassed. Automatic
work starts after board selection: fetch image, verify hash, connect/check MCU, install,
verify where supported, and release the port. Never unplug or navigate away mid-flash.

## Release delivery / deployment

`tools/sync_flasher_releases.py` obtains GitHub's latest stable release and up to two
older stable releases, downloads only the 22 allowlisted firmware names, validates
sizes and GitHub's asset SHA-256 digest when supplied, and generates a catalog with
SHA-256 hashes. Missing target assets remain explicitly unavailable. Prereleases are
not auto-installed. The exact GitHub release downloads are mirrored into the same
Pages deployment: browsers cannot reliably fetch GitHub release binaries cross-origin.
This mirror serves **unmodified release bytes**, not locally compiled firmware.

The existing Pages workflow builds a pinned local vendor bundle and publishes Studio,
flasher, catalog and binaries together. It runs for relevant main-branch changes,
a successful main-branch Release workflow (including manual asset rebuilds), daily
refresh, or manual dispatch. If mirroring fails, the previous Pages deployment stays
intact. One-time repository setting: **Settings → Pages → Build and deployment → Source:
GitHub Actions**. The workflow uploads a Pages artifact and uses the official
`actions/deploy-pages` action; it does not force-push a `gh-pages` branch. GitHub does
not trigger branch-based Pages builds for commits pushed with `GITHUB_TOKEN`.
Changes on a feature branch are not deployed until merged. Replacing a release manually outside the Release
workflow appears on the next daily refresh or manual Pages run.

No credentials or API rate-limit token are embedded in the website. The optional
`GH_TOKEN` is used **only by the CI metadata request**, never forwarded on binary
redirects. Public assets require no browser token. The SHA-256 catalog is transport
integrity, not a signed firmware authenticity scheme; trust is in the repository,
GitHub release and Pages publication chain.

## Local development

From the repository root:

```sh
(cd tools/flasher && npm ci && npm test && npm run build)
python3 tests/test_flasher_releases.py
mkdir -p site/flash
cp tools/studio/index.html tools/studio/*.css tools/studio/*.js site/
cp tools/flasher/index.html tools/flasher/*.css tools/flasher/{app,core,stm32,vendor}.js tools/flasher/boards.json tools/flasher/THIRD_PARTY_LICENSES.txt site/flash/
python3 tools/sync_flasher_releases.py --dest site/flash --limit 1
python3 -m http.server 8000 --directory site
# Open http://localhost:8000/flash/ (not file://).
```

The npm lockfile pins esptool-js, spark-md5 and esbuild. esbuild preserves third-party
license notices in `vendor.js`; full upstream licenses ship as `THIRD_PARTY_LICENSES.txt`. Never commit generated vendor bundles or firmware.
`npm test` exercises validation, target mismatches, checksum errors, UF2 drive guards,
AN3155 handshakes/erase/write/readback, and DfuSe descriptors/write/readback with mocks.
`npm run test:browser` checks the responsive UI, all target selections, permission
cancellation, checksum failure and unavailable-release recovery using fixture images;
run `npx playwright install chromium` once first.

### Required hardware acceptance before calling a transport production-tested

- Flash each ESP family (especially C5/C61/P4, ROM-only/stub differences and native
  USB paths); check MD5, boot, REPL and Studio connect. Wrong chip must refuse erase.
- On each STM32 family, check ROM boot entry and USART1 mapping, GET ID, mass erase,
  write alignment, readback, protection/NACK, timeout, restore normal boot and REPL.
- On a real F411 Black Pill, check DFU descriptors, OS driver setup, erase, download,
  upload/readback, BOOT0/reset and REPL. Wrong flash geometry must refuse erase.
- On all five Pico boards, verify picker permissions, drive identification, UF2 copy,
  disconnect/reboot and REPL. Verify RP2040 ↔ RP2350 mismatch rejection.
- Unplug during connection/erase/write/readback; confirm visible errors and that the
  user can re-enter the bootloader and retry. Never report a failed transfer as success.

Hardware tests cannot be replaced by mocked protocol tests. No attached hardware is
required for unit tests, and no successful hardware flash is claimed by those tests.

## Protocol references

- [Espressif esptool-js](https://github.com/espressif/esptool-js), pinned to 0.7.0.
- [ST AN3155: USART bootloader](https://www.st.com/resource/en/application_note/an3155-usart-protocol-used-in-the-stm32-bootloader-stmicroelectronics.pdf).
- [ST AN3156: USB DFU bootloader](https://www.st.com/resource/en/application_note/cd00264379.pdf).
- [ST AN2606: system-memory boot modes](https://www.st.com/resource/en/application_note/cd00167594.pdf).
- [Raspberry Pi picotool / UF2](https://github.com/raspberrypi/picotool).

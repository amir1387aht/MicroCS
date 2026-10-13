# Actual LVGL static fonts and explicit decoder remedy — checkpoint21

Added LVGL.Font capability, allocation-free/no-init HasFont, typed SetFont with
checked string/selector/arity and main-part FontHeight using real native metrics.
All actual27 public built-ins per8.4/9.4, including version-specific fonts, are
header-derived and configuration-guarded; unavailable fonts false/raise, never
silent substitution. Static library/firmware-owned pointers only; no dynamic font
bytes/raw pointer/deletion/filesystem/custom registration claim.

Final real default8/default9 sanitizers pass835 native checks each; all27-enabled
font8/font9 variants887 each. Native pointer/style readback, exact script metric
matching, actual font rendering, configured/version-specific absence, selector/
arity/type/NUL/length/stale/GC/scope/lifetime/OOM regressions pass ASan/UBSan/leaks.
All five basic/pointer/widgets/constructors/fonts goldens match EACH configuration.
This is not full Unicode layout/glyph API, physical panel, MCU flash or timing proof.

Expanded all8 rendering caught REAL upstream8.4.0 UB at bits_write: left-shifting
negative promoted ~bit_mask. Original failing sanitizer log is preserved; current
inspected release/v8.4 source retains the expression. No sanitizer suppression,
font removal, vendor source rewrite or fake renderer. An EXPLICIT/OFF-by-default
CMake ROOT-only compatibility option verifies exact decoder hash, makes a build-
local copy of only that external unit, changes only unsigned-mask cast and adds
a linked marker. Original checkout/config untouched; generated native unit NOT
committed/vendored. Byte/source provenance recorded, original/fixed hashes tested.
Adapter refuses compressed8 without linked version proof. Wrong major/foreign
prebuilt/unknown-source/no-fix/missing-marker contracts fail as intended,2359296
mask-byte arithmetic cases pass sanitizers. Fixed all8 renders actual compressed
font successfully;9/default uncompressed8 use unchanged library. Other build
routes/compressed prebuilt provenance still unvalidated; dynamic assets unsupported.

Initial standalone fonts.cs incorrectly assumed Hal existed in CMake NONE mode;
real CLI rejected it after native tests passed. Removed only fixture HAL dependency,
not changed build defaults; both versions/final goldens pass. Initial decoder
contract used wrong artifact directory; error preserved and actual lib/liblvgl.a
retry passes. Earlier all8 sanitizer failure was a genuine source defect fixed
explicitly above, never conflated with those invocation/fixture corrections.

Five coherent actual ESP32 standalone/LVGL8/LVGL9/touch/OFF cross-links pass with
new font assertions embedded and source/config/pin/ELF/BIN provenance; compile/
link ONLY, no board execution. Actual TFT RAM/image/copy/ownership131557 checks
EACH normal/GC-stress sanitizer configuration pass. Host85 source/image/XIP,
69scope assertions, formatter/runtime,217 C#templates/218total, OFF/config/package,
old7-per-version dependency/build contracts, exact generated parity and real
browser editor/gallery/full serial Studio pass. CI extended for actual pinned
font generation/all-font sanitizers/contracts, YAML valid; new remote CI pending.
Prior checkpoints18/19/20 each92/92 successful public checks. Last completed broad
full host/profile suite is checkpoint20 evidence on19 source, not relabeled21.
Full LVGL/TFT/core/Studio/security/roadmap/hardware/independent review block release.

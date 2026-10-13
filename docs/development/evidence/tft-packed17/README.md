# Actual native1/4-bit palette/bitmap formats — checkpoint17

Implemented all four exact native depths1/4/8/16, not implicit rounded depths.
Actual upstream palette constructor previously wrote after unchecked calloc;
a small real subclass checks its required native32byte protected palette before
actual createSprite/default-palette initialization. Actual upstream destructor
frees it on successful or failed pixel allocation; no vendor rewrite or substitute
pixel algorithm. Request quota includes padded rows, off-screen pixel and palette.
Strict bit/index/RGB565 ranges, mapped RGB565 versus raw ReadValue, native palette
get/set, parent-wide1bit foreground/background and explicit0-only1bit transparency.
Equal bitmap colors are rejected instead of silent native inversion.

131555 checks pass in EACH normal/GC-stress ASan/UBSan/leak variant: earlier colors,
scopes/lifetimes plus all native default palette entries/index roundtrips/odd bit
and nibble widths, actual C# packed primitives/GLCD, strict ranges and real native
palette OOM versus subsequent pixel OOM, balanced reservation/reuse/destruction.
311804 shared allocation-arithmetic assertions pass normally/under sanitizers.
Five real ESP32 Arduino cross-link variants pass with actual source/config/pin/
artifact hashes; no physical execution/PSRAM/MCU font ABI/transfer proof claim.
An initial matrix/normal tests preceded the new packed capability entries; ALL
final native variants and matrix were rerun after those changes. Only final
coherent source evidence is recorded here, not a mixed old/new matrix proof.

Added legal append-only wire TFT.Palette and TFT.Bitmap, so older sprite-only
firmware cannot incorrectly offer new packed-format templates. Legacy/missing/
false/offline filtering, allocation-free native probes and negative OFF cases
pass. Generated API/help and215 formatted templates include a RAM-only packed
example. Host85 language/source/image/XIP, config/package/docs, actual editor,
all gallery geometry states and full serial-backed Studio pass. Source C# fixture
and RAM template compile; current embedded header parity passes. Full sprite
operations/assets/fonts/readback/DMA/platforms/hardware and broader v2 still
unfinished. No merge/tag/release/provisioning or irreversible device action.

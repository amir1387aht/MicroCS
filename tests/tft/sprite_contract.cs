// Real native RAM/lifetime assertions, invoked by the opt-in Arduino fixture.
// Cross-compilation DOES NOT establish that these assertions ran on a board.
void Check(bool ok, string reason)
{
    if (!ok)
        throw new Exception(reason);
}
var owner = new Tft(); // Native RAM sprites do not require physical Begin.
// Every depth: odd rows, boundaries at64/128, exact row-major semantics.
void CheckImage(Tft parent, int depth)
{
    var target = new TftSprite(parent, 129, 3, depth);
    var values = new int[387];
    for (int i = 0; i < values.Length; i++)
        values[i] = depth == 1 ? i % 2 : depth == 4 ? i % 16 : (i * 173) % 65536;
    values[64] = 0;
    values[128] = depth < 8 ? 1 : 65535;
    target.DrawImage(0, 0, 129, 3, values);
    for (int i = 0; i < values.Length; i++)
    {
        int expected = values[i];
        if (depth == 8)
            expected = ((expected & 0xE000) >> 8) | ((expected & 0x0700) >> 6) | ((expected & 0x0018) >> 3);
        Check(target.ReadValue(i % 129, i / 129) == expected, "row/chunk image depth");
    }
    var list = new List<int>();
    list.Add(1); list.Add(0); list.Add(1); list.Add(0); list.Add(1); list.Add(0);
    target.DrawImage(126, 1, 3, 2, list);
    Check(target.ReadValue(128, 2) == 0, "List subwindow last pixel");
    target.Fill(0);
    var lateBad = new int[] { 1, depth == 1 ? 2 : depth == 4 ? 16 : 65536 };
    try { target.DrawImage(0, 0, 2, 1, lateBad); throw new Exception("bad image accepted"); }
    catch (ArgumentOutOfRangeException e) {}
    Check(target.ReadValue(0, 0) == 0, "late value rejection is atomic");
    try { target.DrawImage(0, 0, 2, 1, new object[] { 1, "bad" }); throw new Exception("mixed image accepted"); }
    catch (ArgumentException e) {}
    Check(target.ReadValue(0, 0) == 0, "late type rejection is atomic");
    try { target.DrawImage(0, 0, 2, 1, list); throw new Exception("wrong length accepted"); }
    catch (ArgumentException e) {}
    try { target.DrawImage(0, 0, 1, 1, null); throw new Exception("null image accepted"); }
    catch (ArgumentException e) {}
    try { target.DrawImage(0, 0, 1, 1, "1"); throw new Exception("string image accepted"); }
    catch (ArgumentException e) {}
    try { target.DrawImage(128, 2, 2, 1, lateBad); throw new Exception("image bounds accepted"); }
    catch (ArgumentOutOfRangeException e) {}
    try { target.DrawImage(-1, 0, 1, 1, new int[] { 0 }); throw new Exception("negative image position accepted"); }
    catch (ArgumentOutOfRangeException e) {}
    try { target.DrawImage(0, 0, 0, 1, new int[0]); throw new Exception("zero width accepted"); }
    catch (ArgumentOutOfRangeException e) {}
    try { target.DrawImage(0, 0, 4096, 4096, new int[0]); throw new Exception("huge image accepted"); }
    catch (ArgumentOutOfRangeException e) {}
    target.Dispose();
    try { target.DrawImage(0, 0, 1, 1, new int[] { 0 }); throw new Exception("disposed image accepted"); }
    catch (ObjectDisposedException e) {}
}
CheckImage(owner, 1); CheckImage(owner, 4); CheckImage(owner, 8); CheckImage(owner, 16);
var limitTarget = new TftSprite(owner, 70, 70);
try { limitTarget.DrawImage(0, 0, 70, 70, new int[0]); throw new Exception("image work limit bypass"); }
catch (ArgumentOutOfRangeException e) {}
limitTarget.Dispose();
// Unbegun physical driver must never silently act like RAM success.
try { owner.DrawImage(0, 0, 1, 1, new int[] { 0 }); throw new Exception("unbegun image accepted"); }
catch (InvalidOperationException e) {}

void CheckCopy(Tft parent, int depth, int targetDepth)
{
    var source = new TftSprite(parent, 129, 3, depth);
    var destination = new TftSprite(parent, 131, 5, targetDepth);
    var pixels = new int[387];
    for (int i = 0; i < pixels.Length; i++)
        pixels[i] = depth == 1 ? i % 2 : depth == 4 ? i % 16 : (i * 173) % 65536;
    source.DrawImage(0, 0, 129, 3, pixels);
    destination.Fill(0);
    if (depth == 4)
    {
        source.SetPaletteColor(3, 0xF800);
        destination.SetPaletteColor(3, 0x07E0);
    }
    source.CopyTo(destination, 1, 1);
    for (int i = 0; i < pixels.Length; i++)
    {
        int expected = pixels[i];
        if (targetDepth == 8)
            expected = ((expected & 0xE000) >> 8) | ((expected & 0x0700) >> 6) | ((expected & 0x0018) >> 3);
        Check(destination.ReadValue(1 + i % 129, 1 + i / 129) == expected, "native copy depth/odd padded rows");
        int original = pixels[i];
        if (depth == 8)
            original = ((original & 0xE000) >> 8) | ((original & 0x0700) >> 6) | ((original & 0x0018) >> 3);
        Check(source.ReadValue(i % 129, i / 129) == original, "copy preserves source");
    }
    Check(destination.ReadValue(0, 0) == 0 && destination.ReadValue(130, 4) == 0, "copy preserves outside pixels");
    if (depth == 4)
        Check(destination.ReadPixel(4, 1) == 0x07E0 && source.ReadPixel(3, 0) == 0xF800, "copy indices not source palette");
    try { source.CopyTo(source, 0, 0); throw new Exception("self copy accepted"); }
    catch (ArgumentException e) {}
    try { source.CopyTo(null, 0, 0); throw new Exception("null copy accepted"); }
    catch (InvalidCastException e) {}
    try { source.CopyTo(parent, 0, 0); throw new Exception("wrong copy class accepted"); }
    catch (InvalidCastException e) {}
    try { source.CopyTo(destination, 3, 0); throw new Exception("copy clipped silently"); }
    catch (ArgumentOutOfRangeException e) {}
    try { source.CopyTo(destination, -1, 0); throw new Exception("negative copy accepted"); }
    catch (ArgumentOutOfRangeException e) {}
    destination.Dispose();
    try { source.CopyTo(destination, 0, 0); throw new Exception("disposed target accepted"); }
    catch (ObjectDisposedException e) {}
    source.Dispose();
    try { source.CopyTo(destination, 0, 0); throw new Exception("disposed source accepted"); }
    catch (ObjectDisposedException e) {}
}
CheckCopy(owner, 1, 1); CheckCopy(owner, 4, 4); CheckCopy(owner, 8, 8);
CheckCopy(owner, 16, 16); CheckCopy(owner, 16, 8);
// Every unsupported depth pair is rejected before destination changes.
var depths = new int[] { 1, 4, 8, 16 };
foreach (int first in depths)
{
    foreach (int second in depths)
    {
        if (first == second || first == 16 && second == 8)
            continue;
        var from = new TftSprite(owner, 2, 2, first);
        var to = new TftSprite(owner, 2, 2, second);
        from.Fill(1); to.Fill(0);
        try { from.CopyTo(to, 0, 0); throw new Exception("unsupported depth pair accepted"); }
        catch (NotSupportedException e) {}
        Check(to.ReadValue(0, 0) == 0, "bad pair destination untouched");
        to.Dispose(); from.Dispose();
    }
}
// Exact4096-pixel acceptance and4097 rejection for both bounded APIs.
var boundary = new TftSprite(owner, 64, 64);
var boundaryTo = new TftSprite(owner, 64, 64);
var boundaryPixels = new int[4096];
for (int i = 0; i < boundaryPixels.Length; i++)
    boundaryPixels[i] = (i * 31) % 65536;
boundary.DrawImage(0, 0, 64, 64, boundaryPixels);
boundary.CopyTo(boundaryTo, 0, 0);
for (int i = 0; i < boundaryPixels.Length; i++)
    Check(boundaryTo.ReadValue(i % 64, i / 64) == boundaryPixels[i], "4096 pixels accepted exactly");
boundaryTo.Dispose(); boundary.Dispose();
var over = new TftSprite(owner, 17, 241); //4097 pixels
var overTo = new TftSprite(owner, 17, 241);
overTo.Fill(0);
try { over.DrawImage(0, 0, 17, 241, new int[0]); throw new Exception("4097 image accepted"); }
catch (ArgumentOutOfRangeException e) {}
try { over.CopyTo(overTo, 0, 0); throw new Exception("4097 copy accepted"); }
catch (ArgumentOutOfRangeException e) {}
Check(overTo.ReadValue(16, 240) == 0, "over-limit copy untouched");
overTo.Dispose(); over.Dispose();

var sprite = new TftSprite(owner, 12, 10);
Check(owner.SpriteCount == 1 && owner.SpriteBytes == 242, "sprite accounting");
Check(sprite.Width == 12 && sprite.Height == 10 && sprite.ColorDepth == 16, "dimensions/depth");
sprite.Fill(0x001F);
Check(sprite.ReadPixel(11, 9) == 0x001F, "fill/read last pixel");
sprite.DrawPixel(4, 3, 0xF800);
Check(sprite.ReadPixel(4, 3) == 0xF800, "native RGB565/endian");
sprite.DrawLine(0, 0, 11, 0, 0x07E0);
Check(sprite.ReadPixel(5, 0) == 0x07E0, "line dispatch");
sprite.FillRect(1, 1, 2, 2, 0xFFFF);
Check(sprite.ReadPixel(2, 2) == 0xFFFF, "rectangle dispatch");
sprite.DrawRect(6, 3, 3, 3, 0x07E0);
Check(sprite.ReadPixel(6, 3) == 0x07E0, "rectangle outline");
sprite.FillCircle(5, 5, 1, 0xF800);
Check(sprite.ReadPixel(5, 5) == 0xF800, "circle dispatch");
sprite.DrawCircle(8, 7, 1, 0xFFFF);
Check(sprite.ReadPixel(9, 7) == 0xFFFF, "circle outline");
sprite.TextColor(0xFFFF, 0);
sprite.TextSize(1);
Check(sprite.DrawText("A", 0, 0, 1) > 0, "compiled GLCD font");
try { sprite.ReadPixel(12, 0); throw new Exception("bad read accepted"); }
catch (ArgumentOutOfRangeException e) {}
try { sprite.Push(0, 0); throw new Exception("unbegun push accepted"); }
catch (InvalidOperationException e) {}
try { owner.Dispose(); throw new Exception("live child parent disposed"); }
catch (InvalidOperationException e) {}
try { var bad = new TftSprite(owner, 4, 4, 3); throw new Exception("unsupported depth accepted"); }
catch (NotSupportedException e) {}
try { var big = new TftSprite(owner, 256, 256); throw new Exception("budget bypass"); }
catch (OutOfMemoryException e) {}
Check(owner.SpriteCount == 1 && owner.SpriteBytes == 242, "failed ctor reservation");
sprite.Dispose();
sprite.Dispose();
Check(owner.SpriteCount == 0 && owner.SpriteBytes == 0, "idempotent release");
try { sprite.Fill(0); throw new Exception("disposed sprite accepted"); }
catch (ObjectDisposedException e) {}
var reduced = new TftSprite(owner, 3, 5, 8);
Check(owner.SpriteBytes == 16 && reduced.ColorDepth == 8, "8bit allocation");
reduced.Fill(0xFFFF);
Check(reduced.ReadPixel(2, 4) == 0xFFFF, "native RGB332 to RGB565");
reduced.Dispose();
// Packed formats use strict indices; their readback returns mapped RGB565.
var palette = new TftSprite(owner, 3, 5, 4);
Check(owner.SpriteBytes == 43 && palette.ColorDepth == 4, "palette budget/odd width");
palette.SetPaletteColor(3, 0xF800);
Check(palette.GetPaletteColor(3) == 0xF800, "native palette entry");
palette.Fill(0);
palette.DrawPixel(2, 4, 3);
Check(palette.ReadValue(2, 4) == 3 && palette.ReadPixel(2, 4) == 0xF800, "odd nibble read");
palette.TextColor(15, 0);
palette.TextSize(1);
Check(palette.DrawText("A", 0, 0, 1) > 0, "palette native font");
try { palette.DrawPixel(0, 0, 16); throw new Exception("palette index truncated"); }
catch (ArgumentOutOfRangeException e) {}
try { palette.GetPaletteColor(16); throw new Exception("bad palette index"); }
catch (ArgumentOutOfRangeException e) {}
try { palette.BitmapColors(0, 1); throw new Exception("wrong bitmap depth"); }
catch (NotSupportedException e) {}
palette.Dispose();
var bitmap = new TftSprite(owner, 9, 3, 1);
var peer = new TftSprite(owner, 5, 1, 1);
Check(owner.SpriteBytes == 9 && bitmap.ColorDepth == 1, "bitmap byte rows");
bitmap.BitmapColors(0x07E0, 0xF800); // Actual mapping belongs to the shared parent.
bitmap.Fill(0);
peer.Fill(1);
Check(peer.ReadPixel(4, 0) == 0x07E0, "parent-wide bitmap mapping");
bitmap.DrawPixel(8, 2, 1);
Check(bitmap.ReadValue(8, 2) == 1 && bitmap.ReadPixel(8, 2) == 0x07E0, "bitmap odd width bit");
Check(bitmap.ReadValue(7, 2) == 0 && bitmap.ReadPixel(7, 2) == 0xF800, "bitmap background");
try { bitmap.DrawPixel(0, 0, 2); throw new Exception("bitmap index truncated"); }
catch (ArgumentOutOfRangeException e) {}
try { bitmap.BitmapColors(1, 1); throw new Exception("implicit bitmap color inversion"); }
catch (ArgumentException e) {}
try { bitmap.SetPaletteColor(0, 1); throw new Exception("wrong palette depth"); }
catch (NotSupportedException e) {}
try { bitmap.ReadPixel(9, 0); throw new Exception("padding bit exposed"); }
catch (ArgumentOutOfRangeException e) {}
try { bitmap.PushTransparent(0, 0, 1); throw new Exception("unsupported bit transparency"); }
catch (NotSupportedException e) {}
bitmap.TextColor(1, 0);
Check(bitmap.DrawText("A", 0, 0, 1) > 0, "bitmap native font");
bitmap.Dispose();
peer.Dispose();
try { var overflow = new TftSprite(owner, 4096, 32, 4); throw new Exception("palette quota overhead omitted"); }
catch (OutOfMemoryException e) {}
try { var overflow = new TftSprite(owner, 4096, 128, 1); throw new Exception("bitmap quota overhead omitted"); }
catch (OutOfMemoryException e) {}
Check(owner.SpriteBytes == 0 && owner.SpriteCount == 0, "packed formats release");
var sprites = new TftSprite[16];
for (int i = 0; i < 16; i++)
    sprites[i] = new TftSprite(owner, 2, 2);
Check(owner.SpriteCount == 16 && owner.SpriteBytes == 160, "shared parent pin");
try { var extra = new TftSprite(owner, 1, 1); throw new Exception("count bypass"); }
catch (OutOfMemoryException e) {}
foreach (var item in sprites)
    item.Dispose();
void Garbage(Tft parent) { var temporary = new TftSprite(parent, 3, 3); }
Garbage(owner);
GC.Collect();
Check(owner.SpriteCount == 0 && owner.SpriteBytes == 0, "unreachable child GC");
owner.Dispose();
TftSprite Orphan()
{
    var parent = new Tft();
    var result = new TftSprite(parent, 3, 3);
    result.Fill(0xF800);
    return result;
}
var orphan = Orphan();
GC.Collect();
Check(orphan.ReadPixel(2, 2) == 0xF800, "child roots parent");
orphan.Dispose();
GC.Collect();
var replacement = new Tft();
replacement.Dispose();
Console.WriteLine("real native sprite assertions finished");

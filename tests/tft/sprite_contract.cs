// Real native RAM/lifetime assertions, invoked by the opt-in Arduino fixture.
// Cross-compilation DOES NOT establish that these assertions ran on a board.
void Check(bool ok, string reason)
{
    if (!ok)
        throw new Exception(reason);
}
var owner = new Tft(); // Native RAM sprites do not require physical Begin.
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

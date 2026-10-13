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
try { var bad = new TftSprite(owner, 4, 4, 4); throw new Exception("unsupported depth accepted"); }
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

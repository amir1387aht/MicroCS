// args: --ramfs 16384
void Check(bool ok) { if (!ok) throw new Exception("stream regression"); }
byte[] original = new byte[150];
for (int i = 0; i < original.Length; i++) original[i] = i;
File.WriteAllBytes("/stream.bin", original);
var input = File.OpenRead("/stream.bin");
Check(input.CanRead && input.CanSeek && !input.CanWrite && input.Length == 150);
byte[] block = new byte[60];
Check(input.Read(block, 5, 50) == 50 && input.Position == 50);
for (int i = 0; i < 50; i++) Check(block[i + 5] == i);
Check(block[0] == 0 && block[59] == 0);
Check(input.Read(block, 0, 50) == 50);
for (int i = 0; i < 50; i++) Check(block[i] == i + 50);
Check(input.Seek(-10, SeekOrigin.End) == 140);
Check(input.Read(block, 0, 50) == 10 && input.ReadByte() == -1);
Check(input.Read(block, 0, 0) == 0);
input.Position = 49;
Check(input.ReadByte() == 49 && input.Seek(-1, SeekOrigin.Current) == 49);
bool bad = false;
try { input.Read(block, -1, 2); } catch (ArgumentOutOfRangeException e) { bad = true; }
Check(bad && input.Position == 49);
bad = false;
try { input.Read(block, 55, 10); } catch (ArgumentOutOfRangeException e) { bad = true; }
Check(bad && input.Position == 49);
bad = false;
try { input.WriteByte(1); } catch (NotSupportedException e) { bad = true; }
Check(bad);
input.Close(); input.Dispose();
Check(!input.CanRead && !input.CanSeek);
bad = false;
try { input.ReadByte(); } catch (ObjectDisposedException e) { bad = true; }
Check(bad);
var output = File.OpenWrite("/stream.bin");
Check(output.CanWrite && !output.CanRead && output.Length == 150);
output.WriteByte(201); output.Position = 50;
output.Write(new byte[] { 202, 203 }, 0, 2); output.Flush(); output.Close();
Check(File.GetLength("/stream.bin") == 150);
input = File.OpenRead("/stream.bin");
Check(input.ReadByte() == 201); input.Position = 50;
Check(input.ReadByte() == 202 && input.ReadByte() == 203); input.Close();
var both = File.Open("/stream.bin", FileMode.Open, FileAccess.ReadWrite);
Check(both.ReadByte() == 201); both.WriteByte(99); both.Position = 0;
Check(both.ReadByte() == 201 && both.ReadByte() == 99); both.Close();
var append = new FileStream("/stream.bin", FileMode.Append);
Check(append.Position == 150); append.WriteByte(204);
bad = false;
try { append.Position = 0; } catch (IOException e) { bad = true; }
Check(bad && append.Position == 151);
append.Seek(10, SeekOrigin.End); append.WriteByte(205);
Check(append.Position == 152 && append.Length == 152); append.Close();
Check(File.GetLength("/stream.bin") == 152);
bad = false;
try { var exists = new FileStream("/stream.bin", FileMode.CreateNew); } catch (IOException e) { bad = true; }
Check(bad && File.GetLength("/stream.bin") == 152);
var created = File.Create("/stream-created.bin");
created.WriteByte(5); created.Position = 4; created.WriteByte(6); created.Close();
var sparse = File.ReadAllBytes("/stream-created.bin");
Check(sparse.Length == 5 && sparse[0] == 5 && sparse[1] == 0 && sparse[3] == 0 && sparse[4] == 6);
var trunc = File.Open("/stream-created.bin", FileMode.Truncate, FileAccess.Write);
Check(trunc.Length == 0); trunc.Close();
var newFile = File.Open("/stream-new.bin", FileMode.CreateNew, FileAccess.Write); newFile.Close();
var empty = File.OpenRead("/stream-new.bin"); Check(empty.ReadByte() == -1); empty.Close();
bad = false;
try { var missing = File.Open("/stream-missing.bin", FileMode.Open); } catch (FileNotFoundException e) { bad = true; }
Check(bad);
for (int i = 0; i < 100; i++) { var f = File.OpenRead("/stream.bin"); Check(f.ReadByte() == 201); f.Dispose(); }
File.Delete("/stream.bin"); File.Delete("/stream-created.bin"); File.Delete("/stream-new.bin");
Console.WriteLine("PASS file streams");

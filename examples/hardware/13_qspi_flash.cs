// 13 · QSPI NOR flash: read the JEDEC id, program a page, quad-read it back, erase.
// QSPI.Read(bus, instruction, address(-1 = none), length[, dummyCycles, dataLines])
// Run: ./mcs --sim examples/hardware/13_qspi_flash.cs
QSPI.Open(0, 50_000_000);

byte[] id = QSPI.Read(0, 0x9F, -1, 3);
Console.WriteLine($"JEDEC id {id[0]:X2} {id[1]:X2} {id[2]:X2} {(id[0] == 0xEF ? "(Winbond)" : "")}");

const int Addr = 0x1000;
QSPI.Command(0, 0x06);                                     // write enable
QSPI.Write(0, 0x02, Addr, Encoding.UTF8.GetBytes("MicroCS"));   // page program
byte[] back = QSPI.Read(0, 0xEB, Addr, 7, 6, 4);           // fast read quad I/O
Console.WriteLine($"read back '{Encoding.UTF8.GetString(back)}'");

QSPI.Command(0, 0x06); QSPI.Command(0, 0x20, Addr);        // 4 KB sector erase
Console.WriteLine($"after erase: 0x{QSPI.Read(0, 0x03, Addr, 1)[0]:X2}");

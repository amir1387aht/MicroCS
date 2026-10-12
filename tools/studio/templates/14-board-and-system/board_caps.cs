// Which peripherals take full settings objects, what runs on DMA, and the
// options only this chip has (shown by Hal.Options).
Console.WriteLine($"Board {Hal.Board}, HAL API {Hal.ApiVersion}");
foreach (var p in new[] { "GPIO", "UART", "I2C", "SPI", "QSPI", "PWM", "ADC" })
    Console.WriteLine($"  {p,-5} {(Hal.Has(p) ? "yes" : "no "),-4} full settings: {Hal.Has(p + ".Configure")}");
Console.WriteLine($"DMA: {Hal.Has("DMA")} (SPI {Hal.Has("DMA.SPI")}, memory {Hal.Has("DMA.Memory")}), channels {Dma.Channels}");
var opts = Hal.Options();
Console.WriteLine($"{opts.Count} board options");
foreach (var o in opts) Console.WriteLine("  " + o);

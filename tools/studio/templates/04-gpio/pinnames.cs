// Pin names are resolved by the port: "LED", "PA5", "GPIO21", "GP15", "P0.13", "A0"...
foreach (var name in new[] { "GPIO2", "GPIO21", "LED" })
{
    try { Console.WriteLine($"{name,-8} -> {GPIO.Pin(name)}"); }
    catch (Exception e) { Console.WriteLine($"{name,-8} -> not on this board ({e.Message})"); }
}

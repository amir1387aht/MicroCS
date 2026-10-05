// MicroCS demo application, compiled to a bytecode image and linked into the
// firmware as a const array (see firmware_example.c / `make example`).
using System;

static class App
{
    static int ticks;
    static Uart uart;
    static int presses;

    public static void Setup()
    {
        uart = new Uart(1, 115200);
        uart.WriteLine("MicroCS app started, board=" + Board.Name);
        Gpio.Mode(Board.LED, Gpio.OUTPUT);
        Board.OnButton(pressed =>
        {
            if (pressed) presses++;
            Console.WriteLine($"[script] button {(pressed ? "down" : "up")} (presses={presses})");
        });
    }

    // called by the firmware main loop every 100 ms
    public static void Loop()
    {
        ticks++;
        Gpio.Write(Board.LED, ticks % 2);
        if (ticks % 3 == 0)
            uart.WriteLine($"tick {ticks}: temp={Sensor.ReadTemp():F1}C bytes waiting={uart.Available}");
    }

    public static int Stats() => ticks * 100 + presses;
}

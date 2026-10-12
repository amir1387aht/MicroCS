// Hardware errors are ordinary C# exceptions.
try
{
    I2C.Open(0);
    I2C.Read(0, 0x3F, 2);                                // nobody at 0x3F? -> IOException (NACK)
    Console.WriteLine("device answered");
}
catch (IOException e) { Console.WriteLine($"I/O error: {e.Message}"); }
catch (TimeoutException) { Console.WriteLine("timeout"); }
catch (NotSupportedException e) { Console.WriteLine($"not on this board: {e.Message}"); }
finally { Console.WriteLine("done"); }

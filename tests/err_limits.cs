// args: --step-limit 20000
// An endless loop is stopped by the step budget; the script cannot catch it
try { long n = 0; while (true) n++; }
catch (Exception) { Console.WriteLine("caught?"); }

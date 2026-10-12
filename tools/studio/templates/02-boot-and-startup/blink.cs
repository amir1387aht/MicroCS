// /blink.cs - started by jobs.cfg ("every 500ms /blink.cs"): it must finish quickly.
// Each run is a fresh program, so keep state in the pin itself (or in a file).
GPIO.Mode(2, GPIO.Output);
GPIO.Toggle(2);

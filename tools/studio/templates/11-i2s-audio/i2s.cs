// 16-bit mono at 16 kHz to an I2S amplifier (MAX98357A...) on bus 0.
I2S.Open(0, 16000, 16, 1);
var buf = new int[100];                                  // 100 samples = 6.25 ms (max 256 bytes per call)
for (int i = 0; i < buf.Length; i++) buf[i] = (int)(Math.Sin(2 * Math.PI * 440 * i / 16000.0) * 8000);
for (int n = 0; n < 160; n++) I2S.WriteSamples(0, buf);  // 1 s of 440 Hz
I2S.Close(0);

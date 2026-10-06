// 12 · I2S audio: synthesize a sine tone and stream it to a DAC/amplifier.
// Run: ./mcs --sim examples/hardware/12_i2s_audio.cs
const int Rate = 16000, Hz = 440;
I2S.Open(0, Rate, 16, 1, I2S.Duplex);           // bus, sample rate, bits, channels, direction

int[] buffer = new int[128];                    // 8 ms of audio (max 256 bytes per call)
for (int i = 0; i < buffer.Length; i++)
    buffer[i] = (int)(Math.Sin(2 * Math.PI * Hz * i / Rate) * 12000);

int sent = 0;
for (int block = 0; block < 5; block++) sent += I2S.WriteSamples(0, buffer);
Console.WriteLine($"streamed {sent} samples ({sent * 1000 / Rate} ms of {Hz} Hz)");

// microphone input works the same way (the simulator loops TX back to RX)
int[] mic = I2S.ReadSamples(0, 4);
Console.WriteLine("first mic samples: " + string.Join(", ", mic));
I2S.Close(0);

// Passive buzzer on PWM channel 2.
int[] notes = { 262, 294, 330, 349, 392, 440, 494, 523 };   // C4 .. C5
int[] beats = { 1, 1, 1, 1, 1, 1, 1, 2 };
for (int i = 0; i < notes.Length; i++)
{
    PWM.Tone(2, notes[i]);
    Thread.Sleep(200 * beats[i]);
    PWM.Stop(2);
    Thread.Sleep(30);
}

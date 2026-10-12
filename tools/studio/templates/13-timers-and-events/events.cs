// Events with your own numbers: C code posts them with mcs_hal_post(MCS_HAL_EV_USER + n, source, value)
// from any ISR; scripts can post them too.
Hal.OnEvent(5, (int source, int value) => Console.WriteLine($"event 5 from {source}: {value}"));
for (int i = 0; i < 3; i++) Hal.Post(5, 1, i * 10);
Hal.Run(100);                                          // dispatch for 100 ms
Console.WriteLine($"dropped events: {Hal.DroppedEvents}");

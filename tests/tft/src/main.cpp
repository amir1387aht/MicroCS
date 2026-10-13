/* Compile/link fixture using actual ESP32 Arduino/TFT_eSPI/LVGL sources.
 * Do not flash the arbitrary fixture pinout onto hardware without review. */
#include <MicroCS.h>
static void out(void *, const char *s, size_t n) { Serial.write((const uint8_t *)s, n); }
void setup() {
    Serial.begin(115200);
    mcs_config_t cfg;
    mcs_config_default(&cfg);
    cfg.write_fn = out;
    cfg.error_fn = out;
    cfg.ticks_fn = mcs_arduino_ticks;
    cfg.delay_fn = mcs_arduino_delay;
    mcs_vm_t *vm = mcs_new(&cfg);
    if (!vm) { Serial.println("VM OOM"); return; }
    const char *source =
#if MCS_ENABLE_TFT_ESPI
        "var d = new Tft(); d.Begin(); d.Rotation = 1; d.Fill(0xF800); "
        "d.DrawPixel(0,0,0xFFFF); d.DrawLine(1,1,20,20,0x07E0); "
        "d.DrawRect(30,30,20,20,0x001F); d.FillRect(60,30,20,20,0xFFFF); "
        "d.DrawCircle(100,100,10,0x001F); d.FillCircle(130,100,10,0x07E0); "
        "d.TextColor(0xFFFF,0); d.TextSize(1); d.DrawText(\"MicroCS\",8,8,1); "
#if defined(TOUCH_CS)
        "d.SetTouch(200,3600,200,3600,0); "
#endif
#if MCS_ENABLE_LVGL
        "d.AttachLVGL(); var g = new LvGui(d.Width,d.Height); "
        "var label = g.Screen.Create(\"label\"); label.Text = \"LVGL + TFT\"; "
        "g.Pump(40); g.Dispose(); d.DetachLVGL(); "
#endif
        "d.Dispose(); Console.WriteLine(\"fixture finished\");"
#else
        "Console.WriteLine(\"graphics OFF fixture\");"
#endif
        ;
    mcs_result_t result = mcs_exec_source(vm, "tft-fixture.cs", source);
    if (result != MCS_OK) Serial.println(mcs_last_error(vm));
    mcs_free(vm);
}
void loop() {}

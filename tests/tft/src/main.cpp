/* Compile/link fixture using actual ESP32 Arduino/TFT_eSPI/LVGL sources.
 * Do not flash the arbitrary fixture pinout onto hardware without review. */
#include <MicroCS.h>
#if MCS_ENABLE_TFT_ESPI
#include "mcs_tft.h"
#if MCS_ENABLE_LVGL
#include "mcs_lvgl.h"
#endif
#endif
#if MCS_ENABLE_TFT_ESPI
#include "sprite_contract.h"
#endif
#if MCS_ENABLE_TFT_ESPI && MCS_ENABLE_LVGL
static uint64_t fixture_tft_scope;
static mcs_value_t try_end_tft_scope(mcs_vm_t *vm, mcs_value_t self, int argc, mcs_value_t *argv) {
    (void)self;
    (void)argc;
    (void)argv;
    return mcs_int(mcs_tft_scope_end(vm, fixture_tft_scope));
}
#endif
static void out(void *, const char *s, size_t n) {
    Serial.write((const uint8_t *)s, n);
}
void setup() {
    Serial.begin(115200);
    mcs_config_t cfg;
    mcs_config_default(&cfg);
    cfg.write_fn = out;
    cfg.error_fn = out;
    cfg.ticks_fn = mcs_arduino_ticks;
    cfg.delay_fn = mcs_arduino_delay;
    mcs_vm_t *vm = mcs_new(&cfg);
    if (!vm) {
        Serial.println("VM OOM");
        return;
    }
#if MCS_ENABLE_TFT_ESPI
    mcs_result_t contract = mcs_exec_source(vm, "sprite-contract.cs", sprite_contract);
    if (contract != MCS_OK) {
        Serial.println(mcs_last_error(vm));
        mcs_free(vm);
        return;
    }
#endif
#if MCS_ENABLE_TFT_ESPI
    uint64_t tft_scope = mcs_tft_scope_begin(vm);
#if MCS_ENABLE_LVGL
    uint32_t gui_scope = mcs_lvgl_scope_begin(vm);
    fixture_tft_scope = tft_scope;
    mcs_register_function(vm, "TryEndTftScope", try_end_tft_scope, 0);
#endif
#endif
    const char *source =
#if MCS_ENABLE_TFT_ESPI
        "var d = new Tft(); d.Begin(); d.Rotation = 1; d.Fill(0xF800); "
        "var s = new TftSprite(d,32,16); s.Fill(0x001F); s.DrawPixel(0,0,0xF800); "
        "s.Push(10,10); s.PushTransparent(10,30,0x001F); s.Dispose(); "
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
        "if(!LvGui.HasFont(\"Default\")) throw new Exception(\"native default font missing\"); "
        "label.SetFont(\"Default\"); if(label.FontHeight<=0) throw new Exception(\"native font "
        "metric\"); "
        "if(TryEndTftScope()!=-1) throw new Exception(\"live GUI backend scope guard\"); "
        "g.Pump(40); g.Dispose(); d.DetachLVGL(); "
#endif
        "var surviving=new TftSprite(d,8,8); surviving.Fill(0xF800); "
        "Console.WriteLine(\"fixture drawing calls finished\");"
#else
        "Console.WriteLine(\"graphics OFF fixture\");"
#endif
        ;
    mcs_result_t result = mcs_exec_source(vm, "tft-fixture.cs", source);
    if (result != MCS_OK)
        Serial.println(mcs_last_error(vm));
#if MCS_ENABLE_TFT_ESPI
#if MCS_ENABLE_LVGL
    mcs_lvgl_scope_end(vm, gui_scope);
#endif
    if (mcs_tft_scope_end(vm, tft_scope))
        Serial.println("TFT scope blocked unexpectedly");
    if (result == MCS_OK) {
        result = mcs_exec_source(vm, "scope-closed.cs",
                                 "try { var n=d.SpriteCount; throw new Exception(\"live scope "
                                 "driver\"); } catch(ObjectDisposedException e) {} "
                                 "try { surviving.ReadPixel(0,0); throw new Exception(\"live scope "
                                 "sprite\"); } catch(ObjectDisposedException e) {} "
                                 "var replacement=new Tft(); replacement.Dispose(); "
                                 "Console.WriteLine(\"fixture scope assertions finished\");");
        if (result != MCS_OK)
            Serial.println(mcs_last_error(vm));
    }
#endif
    mcs_free(vm);
}
void loop() {}

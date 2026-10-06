// MicroCS inside a normal sketch: C++ keeps control, C# runs a script and
// is called from loop(). The script can be changed without touching C++.
#include <MicroCS.h>

static const char* script = R"CS(
int count = 0;
void Tick() {
    count++;
    GPIO.Write(13, count % 2);
    if (count % 10 == 0) Console.WriteLine($"tick {count}, up {Environment.TickCount} ms");
}
GPIO.Mode(13, GPIO.Output);
Console.WriteLine("script ready");
)CS";

static uint8_t heap[48 * 1024] __attribute__((aligned(8)));
static mcs_pool_t pool;
static mcs_vm_t* vm;
static mcs_hal_t hal;

static void out(void*, const char* s, size_t n) { Serial.write((const uint8_t*)s, n); }

void setup() {
  Serial.begin(115200);
  mcs_pool_init(&pool, heap, sizeof heap);
  mcs_config_t cfg;
  mcs_config_default(&cfg);
  cfg.realloc_fn = mcs_pool_realloc;
  cfg.alloc_ud = &pool;
  cfg.write_fn = out;
  cfg.ticks_fn = mcs_arduino_ticks;
  cfg.delay_fn = mcs_arduino_delay;
  vm = mcs_new(&cfg);
  mcs_arduino_cfg_t pins = MCS_ARDUINO_CFG_DEFAULT;
  mcs_arduino_hal_init(&hal, &pins);
  mcs_hal_open_lib(vm, &hal);
  if (mcs_exec_source(vm, "app.cs", script) != MCS_OK) Serial.println(mcs_last_error(vm));
}

void loop() {
  if (mcs_call(vm, "Tick", 0, NULL, NULL) != MCS_OK) Serial.println(mcs_last_error(vm));
  delay(100);
}

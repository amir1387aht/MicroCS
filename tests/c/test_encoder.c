/* Quadrature is decoded before the queue, not in a delayed C# callback. */
#include "mcs_hal.h"
#include <stdio.h>
#include <string.h>
#if MCS_OS == MCS_OS_POSIX
#include <pthread.h>
#endif
static int checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); } } while (0)
static mcs_hal_t hal;
static mcs_hal_sim_t sim;
#if MCS_ENABLE_ENCODER
static void run(mcs_vm_t* vm, const char* code) {
    mcs_result_t rc = mcs_exec_source(vm, "encoder.cs", code);
    CHECK(rc == MCS_OK);
    if (rc != MCS_OK) fprintf(stderr,"%s\n",mcs_last_error(vm));
}
static void expect(mcs_vm_t* vm, int n) {
    char code[180]; snprintf(code,sizeof code,"if (enc.Position != %d) throw new Exception(\"position\");",n); run(vm,code);
}
static void raw(unsigned state) {
    sim.inputs[2] = state & 2 ? 1 : 2;
    sim.inputs[3] = state & 1 ? 1 : 2;
}
static void step(unsigned state) { raw(state); CHECK(mcs_hal_gpio_edge(&sim,2)); }
static void turn(int a, int b, int reverse) {
    mcs_hal_sim_set_input(&sim, reverse ? a : b, 0);
    mcs_hal_sim_set_input(&sim, reverse ? b : a, 0);
    mcs_hal_sim_set_input(&sim, reverse ? a : b, 1);
    mcs_hal_sim_set_input(&sim, reverse ? b : a, 1);
}
#if MCS_OS == MCS_OS_POSIX
static void* spin_encoder(void* unused) {
    (void)unused;
    for (int i=0;i<10000;i++) turn(2,3,0);
    return NULL;
}
#endif
static int (*real_irq)(void*,int,int);
static int fail_pin = -1;
static int faulty_irq(void* ctx,int pin,int edge) {
    if (pin == fail_pin && edge) return MCS_HAL_EBUSY;
    return real_irq(ctx,pin,edge);
}
#endif
static mcs_vm_t* newvm(void) {
    mcs_config_t cfg; mcs_config_default(&cfg);
    mcs_vm_t* vm = mcs_new(&cfg); CHECK(vm != NULL);
    mcs_hal_open_lib(vm,&hal); return vm;
}
int main(void) {
    mcs_hal_sim_init(&hal,&sim);
#if MCS_ENABLE_ENCODER
    real_irq = hal.gpio_irq;
#endif
    mcs_vm_t* vm = newvm();
#if MCS_ENABLE_ENCODER
    CHECK(mcs_capability_has(vm,"RotaryEncoder"));
    run(vm,"var enc = new RotaryEncoder(2,3,4,4); if (enc.Position != 0 || enc.ButtonPressed) throw new Exception();");
    CHECK(sim.irq[2] == 3 && sim.irq[3] == 3 && sim.irq[4] == 0);
    mcs_hal_sim_set_input(&sim,4,0); run(vm,"if (!enc.ButtonPressed) throw new Exception();");
    mcs_hal_sim_set_input(&sim,4,1);
    /* 10k detents while the VM does not poll, and with an already-full ring. */
    while (mcs_hal_post(MCS_HAL_EV_USER,0,0)) {}
    uint32_t dropped = mcs_hal_dropped_events();
    for (int i=0;i<10000;i++) turn(2,3,0);
    CHECK(mcs_hal_dropped_events() == dropped);
    expect(vm,10000);
    for (int i=0;i<10000;i++) turn(2,3,1);
    expect(vm,0);
#if MCS_OS == MCS_OS_POSIX
    pthread_t producer;
    CHECK(pthread_create(&producer,NULL,spin_encoder,NULL) == 0);
    run(vm,"for (int i=0;i<10000;i++) { int n = enc.Position; }");
    CHECK(pthread_join(producer,NULL) == 0);
    expect(vm,10000);
#endif
    run(vm,"enc.Reset(17); GC.Collect();"); expect(vm,17);
    /* Reverse motion and contact chatter cancel without time-based filtering. */
    mcs_hal_sim_set_input(&sim,3,0); mcs_hal_sim_set_input(&sim,3,1);
    expect(vm,17);
    mcs_hal_sim_set_input(&sim,3,0); mcs_hal_sim_set_input(&sim,2,0);
    expect(vm,17);
    mcs_hal_sim_set_input(&sim,2,1); mcs_hal_sim_set_input(&sim,3,1);
    expect(vm,17);
    run(vm,"try { GPIO.Off(2); throw new Exception(); } catch (InvalidOperationException e) {} "
           "try { GPIO.OnChange(3,GPIO.Both,(bool x)=>{}); throw new Exception(); } catch (InvalidOperationException e) {} "
           "try { GPIO.Mode(2,GPIO.Output); throw new Exception(); } catch (InvalidOperationException e) {} "
           "try { GPIO.Write(3,1); throw new Exception(); } catch (InvalidOperationException e) {} "
           "try { GPIO.Toggle(2); throw new Exception(); } catch (InvalidOperationException e) {} "
           "try { new Pin(4,GPIO.Output); throw new Exception(); } catch (InvalidOperationException e) {} "
           "try { GPIO.Configure(2,new GpioConfig()); throw new Exception(); } catch (InvalidOperationException e) {} "
           "try { new RotaryEncoder(2,5); throw new Exception(); } catch (InvalidOperationException e) {} "
           "enc.Dispose(); enc.Dispose(); "
           "try { int n = enc.Position; throw new Exception(); } catch (ObjectDisposedException e) {} "
           "try { enc.Reset(); throw new Exception(); } catch (ObjectDisposedException e) {} "
           "try { bool b = enc.ButtonPressed; throw new Exception(); } catch (ObjectDisposedException e) {}");
    CHECK(sim.irq[2] == 0 && sim.irq[3] == 0);
    /* Every transition (including duplicate and diagonal), each initial phase. */
    static const int dirs[16]={0,1,-1,0,-1,0,0,1,1,0,0,-1,0,-1,1,0};
    for (int old=0;old<4;old++) for(int next=0;next<4;next++) {
        raw((unsigned)old); run(vm,"enc = new RotaryEncoder(2,3,1);");
        step((unsigned)next); expect(vm,dirs[old*4+next]); run(vm,"enc.Dispose();");
    }
    raw(3); run(vm,"enc = new RotaryEncoder(2,3,2,-1,true);");
    turn(2,3,0); expect(vm,-2); turn(2,3,1); expect(vm,0); run(vm,"enc.Dispose();");
    raw(3); run(vm,"enc = new RotaryEncoder(2,3,4);");
    step(2); step(1); /* illegal two-bit jump clears partial */
    step(3); expect(vm,0);
    run(vm,"enc.Reset(2147483647);"); turn(2,3,0); expect(vm,2147483647);
    turn(2,3,1); expect(vm,2147483646);
    run(vm,"enc.Reset(-2147483647 - 1);"); turn(2,3,1); expect(vm,(-2147483647-1));
    turn(2,3,0); expect(vm,(-2147483647));
    run(vm,"enc.Dispose(); "
           "try { new RotaryEncoder(2,2); throw new Exception(); } catch (ArgumentOutOfRangeException e) {} "
           "try { new RotaryEncoder(2,3,3); throw new Exception(); } catch (ArgumentOutOfRangeException e) {} "
           "try { new RotaryEncoder(2,3,4,2); throw new Exception(); } catch (ArgumentOutOfRangeException e) {} "
           "GPIO.OnChange(2,GPIO.Both,(bool x)=>{}); "
           "try { new RotaryEncoder(2,3); throw new Exception(); } catch (InvalidOperationException e) {} GPIO.Off(2);");
    /* Exhausting pin roots must fail before hardware is touched. */
    int roots[256], nr = 0, handle;
    while ((handle = mcs_pin(vm,mcs_bool(true))) >= 0 && nr < 256) roots[nr++] = handle;
    CHECK(mcs_exec_source(vm,"pin_oom.cs","new RotaryEncoder(2,3);") != MCS_OK);
    CHECK(sim.irq[2] == 0 && sim.irq[3] == 0);
    for (int i=0;i<nr;i++) mcs_unpin(vm,roots[i]);
    /* Failed second IRQ leaves first disabled, pin unpinned, next open succeeds. */
    hal.gpio_irq = faulty_irq; fail_pin = 3;
    for(int i=0;i<100;i++) run(vm,"try { new RotaryEncoder(2,3); throw new Exception(); } catch (IOException e) {}");
    CHECK(sim.irq[2] == 0 && sim.irq[3] == 0);
    fail_pin = -1; hal.gpio_irq = real_irq;
    run(vm,"for(int i=0;i<1000;i++) { var e = new RotaryEncoder(2,3); e.Dispose(); } GC.Collect();");
    run(vm,"enc = new RotaryEncoder(2,3); var e2 = new RotaryEncoder(5,6); var e3 = new RotaryEncoder(7,8); var e4 = new RotaryEncoder(9,10); "
           "try { new RotaryEncoder(11,12); throw new Exception(); } catch (IOException e) {} "
           "e2.Dispose(); e3.Dispose(); e4.Dispose();");
    mcs_hal_close_lib(vm); CHECK(sim.irq[2] == 0 && sim.irq[3] == 0);
    CHECK(!mcs_hal_gpio_edge(&sim,2));
    mcs_hal_open_lib(vm,&hal); run(vm,"enc = new RotaryEncoder(2,3);");
    /* Rebinding the HAL detaches all of the old VM's encoders. */
    mcs_hal_open_lib(vm,&hal); CHECK(sim.irq[2] == 0 && sim.irq[3] == 0);
    run(vm,"enc = new RotaryEncoder(2,3);");
    mcs_free(vm); /* finalizer also covers applications not using close_lib */
    CHECK(sim.irq[2] == 0 && sim.irq[3] == 0); CHECK(!mcs_hal_gpio_edge(&sim,2));
    vm = newvm();
    mcs_hal_open_lib_ex(vm,&hal,MCS_HAL_NO_EVENTS);
    run(vm,"try { new RotaryEncoder(2,3); throw new Exception(); } catch (InvalidOperationException e) {}");
    CHECK(sim.irq[2] == 0 && sim.irq[3] == 0);
    CHECK(!mcs_capability_has(vm,"RotaryEncoder"));
#else
    CHECK(!mcs_capability_has(vm,"RotaryEncoder"));
    CHECK(!mcs_hal_gpio_edge(&sim,2));
#endif
    mcs_hal_close_lib(vm); mcs_free(vm);
#if MCS_ENABLE_ENCODER
    /* Fixed-pool teardown and many explicit disposals: no hidden SDK mallocs. */
    static uint8_t heap[64 * 1024];
    mcs_pool_t pool; mcs_pool_init(&pool,heap,sizeof heap);
    mcs_config_t cfg; mcs_config_default(&cfg);
    cfg.realloc_fn = mcs_pool_realloc; cfg.alloc_ud = &pool;
    vm = mcs_new(&cfg); CHECK(vm != NULL);
    mcs_hal_open_lib(vm,&hal);
    run(vm,"for (int i=0;i<1000;i++) { var e = new RotaryEncoder(2,3); e.Dispose(); } GC.Collect();");
    CHECK(sim.irq[2] == 0 && sim.irq[3] == 0);
    run(vm,"var enc = new RotaryEncoder(2,3);");
    mcs_hal_close_lib(vm); mcs_free(vm);
    CHECK(pool.used == 0);
    CHECK(!mcs_hal_gpio_edge(&sim,2));
#endif
    printf("%d/%d encoder checks passed\n",checks-failures,checks);
    return failures ? 1 : 0;
}

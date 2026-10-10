/*
 * MicroCS - built-in "servo" driver: the C# Servo class (front end) for hobby
 * servos (SG90, MG90S, MG996R, ...) and ESCs.
 *
 *   var s = new Servo(0);                    PWM channel 0, 500..2500 us = 0..180 degrees
 *   var s = new Servo(0, 544, 2400);         pulse range of your servo (Arduino's defaults)
 *   var s = new Servo(0, 1000, 2000, 90);    pulse range and travel in degrees
 *   s.Angle = 90;  s.Write(45);              degrees (int or float), ArgumentOutOfRange outside 0..MaxAngle
 *   s.Pulse = 1500; s.WritePulse(1500);      microseconds, MinPulse..MaxPulse
 *   s.MoveTo(180, 1000);                     sweep to 180 degrees in 1 s (smooth ease in/out, blocks)
 *   s.Detach(); s.Attach();                  stop / resume the 50 Hz signal
 *   s.Attached  s.Angle  s.Pulse  s.MinPulse  s.MaxPulse  s.MaxAngle  s.Channel  s.Dispose()
 *
 * The front end turns angles into pulse widths and talks to the backend of the
 * driver registered as "servo" (mcs_servo_ops_t): by default the board's HAL PWM
 * (50 Hz, channel = PWM channel), which every port provides. A firmware can
 * register its own backend - a PCA9685 I2C driver, a servo bus - with
 * mcs_driver_register(); see include/mcs_driver.h. Off with MCS_ENABLE_SERVO 0.
 */
#include "mcs_driver.h"
#if MCS_ENABLE_SERVO

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#define NATIVE(name) static mcs_value_t name(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv)

#ifndef MCS_SERVO_PERIOD_US
#define MCS_SERVO_PERIOD_US 20000     /* 50 Hz frame */
#endif
#ifndef MCS_SERVO_FRAME_MS
#define MCS_SERVO_FRAME_MS 20         /* MoveTo update interval */
#endif
#ifndef MCS_SERVO_MOVE_MAX_MS
#define MCS_SERVO_MOVE_MAX_MS 600000
#endif

/* ---- default backend: the HAL's PWM ---- */
static int hal_set_pulse(void* ctx, int channel, uint32_t pulse_us, uint32_t period_us) {
    const mcs_hal_t* h = (const mcs_hal_t*)ctx;
    if (!h || !period_us) return MCS_HAL_ENOTSUP;
    uint32_t freq = (1000000u + period_us / 2) / period_us;
    uint32_t duty16 = (uint32_t)(((uint64_t)pulse_us * 65535u + period_us / 2) / period_us);
    if (duty16 > 65535u) duty16 = 65535u;
    if (h->pwm_set16) return h->pwm_set16(h->ctx, channel, freq, (uint16_t)duty16);
    if (h->pwm_set) return h->pwm_set(h->ctx, channel, freq, (uint16_t)((duty16 * 1000u + 32767u) / 65535u));
    return MCS_HAL_ENOTSUP;
}
static int hal_stop(void* ctx, int channel) {
    const mcs_hal_t* h = (const mcs_hal_t*)ctx;
    if (!h) return MCS_HAL_ENOTSUP;
    if (h->pwm_stop) return h->pwm_stop(h->ctx, channel);
    if (h->pwm_set16) return h->pwm_set16(h->ctx, channel, 50, 0);
    if (h->pwm_set) return h->pwm_set(h->ctx, channel, 50, 0);
    return MCS_HAL_ENOTSUP;
}
static const mcs_servo_ops_t hal_ops = { hal_set_pulse, hal_stop };
static mcs_driver_t hal_driver = MCS_SERVO_DRIVER(&hal_ops, NULL);

void mcs_servo_use_hal(const mcs_hal_t* hal) {
    if (!hal || (!hal->pwm_set && !hal->pwm_set16)) return;
    mcs_driver_register_default(&hal_driver);                 /* keeps a "servo" you registered first */
    if (mcs_driver_find("servo") == &hal_driver) hal_driver.ctx = (void*)hal;
}

/* ---- C# Servo ---- */
typedef struct {
    int channel, min_us, max_us, max_angle;
    int pulse;                     /* last pulse written, 0 = none yet */
    uint8_t attached, disposed;
    const mcs_driver_t* drv;
} servo_t;
static const mcs_class_def_t servo_def;

static void servo_ctor(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    servo_t* s = (servo_t*)mcs_userdata(self);
    if (argc < 1 || argc == 2 || argc > 4) { mcs_raise(vm, "ArgumentException", "Servo(channel[, minUs, maxUs[, maxAngle]]) expected"); return; }
    int ch = (int)mcs_to_int(vm, argv[0]); if (mcs_has_exception(vm)) return;
    int lo = 500, hi = 2500, ang = 180;
    if (argc >= 3) {
        lo = (int)mcs_to_int(vm, argv[1]); if (mcs_has_exception(vm)) return;
        hi = (int)mcs_to_int(vm, argv[2]); if (mcs_has_exception(vm)) return;
    }
    if (argc == 4) { ang = (int)mcs_to_int(vm, argv[3]); if (mcs_has_exception(vm)) return; }
    if (ch < 0) { mcs_raise(vm, "ArgumentOutOfRangeException", "Servo channel out of range"); return; }
    if (lo < 1 || hi > MCS_SERVO_PERIOD_US || lo >= hi) { mcs_raise(vm, "ArgumentOutOfRangeException", "Servo pulse range out of range (1..%d us, min < max)", MCS_SERVO_PERIOD_US); return; }
    if (ang < 1 || ang > 3600) { mcs_raise(vm, "ArgumentOutOfRangeException", "Servo travel out of range (1..3600 degrees)"); return; }
    s->channel = ch; s->min_us = lo; s->max_us = hi; s->max_angle = ang;
    s->drv = mcs_driver_find("servo");
    if (!s->drv || !s->drv->ops) mcs_hal_raise(vm, "Servo", MCS_HAL_ENOTSUP);
}
static void servo_free(mcs_vm_t* vm, void* data) { (void)vm; (void)data; }   /* an unreferenced servo keeps its position */

#define SV() servo_t* s = (servo_t*)mcs_check_userdata(vm, self, &servo_def); if (!s) return mcs_null(); \
    if (s->disposed) { mcs_raise(vm, "ObjectDisposedException", "Servo"); return mcs_null(); }
#define OPS() ((const mcs_servo_ops_t*)s->drv->ops)

static int sv_out(mcs_vm_t* vm, servo_t* s, int pulse) {
    const mcs_servo_ops_t* ops = s->drv && s->drv->ops ? OPS() : NULL;
    if (!ops || !ops->set_pulse) { mcs_hal_raise(vm, "Servo", MCS_HAL_ENOTSUP); return -1; }
    int rc = ops->set_pulse(s->drv->ctx, s->channel, (uint32_t)pulse, MCS_SERVO_PERIOD_US);
    if (rc < 0) { mcs_hal_raise(vm, "Servo", rc); return -1; }
    s->pulse = pulse; s->attached = 1;
    return 0;
}
/* angle argument (int, or float with MCS_ENABLE_FLOAT) -> pulse; -1 with an exception raised */
static int pulse_arg(mcs_vm_t* vm, servo_t* s, mcs_value_t v) {
#if MCS_ENABLE_FLOAT
    mcs_float_t a = mcs_to_float(vm, v);
    if (mcs_has_exception(vm)) return -1;
    if (!(a >= 0 && a <= (mcs_float_t)s->max_angle)) { mcs_raise(vm, "ArgumentOutOfRangeException", "Servo angle outside 0..%d", s->max_angle); return -1; }
    return s->min_us + (int)((mcs_float_t)(s->max_us - s->min_us) * a / (mcs_float_t)s->max_angle + (mcs_float_t)0.5);
#else
    mcs_int_t a = mcs_to_int(vm, v);
    if (mcs_has_exception(vm)) return -1;
    if (a < 0 || a > s->max_angle) { mcs_raise(vm, "ArgumentOutOfRangeException", "Servo angle outside 0..%d", s->max_angle); return -1; }
    return s->min_us + (int)((((int32_t)(s->max_us - s->min_us)) * (int32_t)a + s->max_angle / 2) / s->max_angle);
#endif
}
static int us_arg(mcs_vm_t* vm, servo_t* s, mcs_value_t v) {
    mcs_int_t us = mcs_to_int(vm, v);
    if (mcs_has_exception(vm)) return -1;
    if (us < s->min_us || us > s->max_us) { mcs_raise(vm, "ArgumentOutOfRangeException", "Servo pulse outside %d..%d us", s->min_us, s->max_us); return -1; }
    return (int)us;
}

NATIVE(sv_write) { SV(); int p = pulse_arg(vm, s, argv[0]); if (p < 0) return mcs_null(); sv_out(vm, s, p); return mcs_null(); }
NATIVE(sv_getangle) {
    SV();
    if (!s->pulse) return mcs_int(0);
    int32_t span = s->max_us - s->min_us;
    int a = (int)(((int32_t)(s->pulse - s->min_us) * s->max_angle + span / 2) / span);
    return mcs_int(a);
}
NATIVE(sv_writepulse) { SV(); int p = us_arg(vm, s, argv[0]); if (p < 0) return mcs_null(); sv_out(vm, s, p); return mcs_null(); }
NATIVE(sv_getpulse) { SV(); return mcs_int(s->pulse); }
NATIVE(sv_moveto) {   /* MoveTo(angle, ms): smoothstep ease in/out in MCS_SERVO_FRAME_MS steps */
    SV();
    int target = pulse_arg(vm, s, argv[0]); if (target < 0) return mcs_null();
    mcs_int_t ms = mcs_to_int(vm, argv[1]); if (mcs_has_exception(vm)) return mcs_null();
    if (ms < 0 || ms > MCS_SERVO_MOVE_MAX_MS) { mcs_raise(vm, "ArgumentOutOfRangeException", "Servo.MoveTo time out of range"); return mcs_null(); }
    int from = s->pulse;
    int frames = (int)(ms / MCS_SERVO_FRAME_MS);
    if (!from || !frames) { sv_out(vm, s, target); if (frames == 0 && ms) mcs_sleep(vm, (uint32_t)ms); return mcs_null(); }
    for (int i = 1; i <= frames; i++) {
        uint32_t t = (uint32_t)i * 1024u / (uint32_t)frames;            /* 0..1024 */
        uint32_t e = t * t * (3u * 1024u - 2u * t) >> 20;               /* smoothstep, 0..1024 */
        int p = from + (int)(((int32_t)(target - from) * (int32_t)e) / 1024);
        if (i == frames) p = target;
        if (sv_out(vm, s, p)) return mcs_null();
        mcs_sleep(vm, MCS_SERVO_FRAME_MS);
        if (mcs_has_exception(vm)) return mcs_null();
    }
    uint32_t rest = (uint32_t)(ms - (mcs_int_t)frames * MCS_SERVO_FRAME_MS);
    if (rest) mcs_sleep(vm, rest);
    return mcs_null();
}
NATIVE(sv_attach) {
    SV();
    if (!s->attached && s->pulse) sv_out(vm, s, s->pulse);
    return mcs_null();
}
NATIVE(sv_detach) {
    SV();
    const mcs_servo_ops_t* ops = s->drv && s->drv->ops ? OPS() : NULL;
    if (ops && ops->stop && s->attached) {
        int rc = ops->stop(s->drv->ctx, s->channel);
        if (rc < 0) return mcs_hal_raise(vm, "Servo.Detach", rc);
    }
    s->attached = 0;
    return mcs_null();
}
NATIVE(sv_attached) { SV(); return mcs_bool(s->attached); }
NATIVE(sv_min) { SV(); return mcs_int(s->min_us); }
NATIVE(sv_max) { SV(); return mcs_int(s->max_us); }
NATIVE(sv_maxangle) { SV(); return mcs_int(s->max_angle); }
NATIVE(sv_channel) { SV(); return mcs_int(s->channel); }
NATIVE(sv_dispose) {
    servo_t* s = (servo_t*)mcs_check_userdata(vm, self, &servo_def); if (!s) return mcs_null();
    if (!s->disposed) {
        const mcs_servo_ops_t* ops = s->drv && s->drv->ops ? OPS() : NULL;
        if (ops && ops->stop && s->attached) ops->stop(s->drv->ctx, s->channel);
        s->attached = 0; s->disposed = 1;
    }
    return mcs_null();
}
static const mcs_reg_t servo_members[] = {
    MCS_GET("Angle", sv_getangle), MCS_SET("Angle", sv_write), MCS_FN("Write", sv_write, 1), MCS_FN("Read", sv_getangle, 0),
    MCS_GET("Pulse", sv_getpulse), MCS_SET("Pulse", sv_writepulse), MCS_FN("WritePulse", sv_writepulse, 1),
    MCS_FN("MoveTo", sv_moveto, 2), MCS_FN("Attach", sv_attach, 0), MCS_FN("Detach", sv_detach, 0), MCS_FN("Stop", sv_detach, 0),
    MCS_GET("Attached", sv_attached), MCS_GET("MinPulse", sv_min), MCS_GET("MaxPulse", sv_max),
    MCS_GET("MaxAngle", sv_maxangle), MCS_GET("Channel", sv_channel), MCS_FN("Dispose", sv_dispose, 0),
    MCS_REG_END
};
static const mcs_class_def_t servo_def = { "Servo", sizeof(servo_t), servo_ctor, servo_free, servo_members, NULL };

void mcs_servo_open(mcs_vm_t* vm, const mcs_driver_t* drv) {
    (void)drv;
    mcs_register_class(vm, &servo_def);
}
#endif

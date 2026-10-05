/*
 * MicroCS - helper macros for wrapping existing C functions.
 *
 *   int  hal_gpio_read(int pin);
 *   void hal_gpio_write(int pin, int level);
 *   MCS_WRAP_I_I (w_gpio_read,  hal_gpio_read)
 *   MCS_WRAP_V_II(w_gpio_write, hal_gpio_write)
 *   static const mcs_reg_t gpio[] = {
 *       MCS_FN("Read", w_gpio_read, 1), MCS_FN("Write", w_gpio_write, 2), MCS_REG_END };
 *   mcs_register_module(vm, "Gpio", gpio);
 *
 * Naming: MCS_WRAP_<ret>_<args>  with  V=void I=integer F=float B=bool S=const char*
 * Argument conversion errors raise ArgumentException in the script.
 */
#ifndef MCS_BIND_H
#define MCS_BIND_H
#include "mcs.h"

#define MCS__SIG(name) static mcs_value_t name(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv)
#define MCS__U (void)self; (void)argc; (void)argv; (void)vm
#define MCS__I(n) mcs_to_int(vm, argv[n])
#define MCS__B(n) mcs_to_bool(vm, argv[n])
#define MCS__S(n) mcs_to_cstr(vm, argv[n])
#define MCS__F(n) mcs_to_float(vm, argv[n])
#define MCS__RET_IF_EXC() if (mcs_has_exception(vm)) return mcs_null()

/* ---- void results */
#define MCS_WRAP_V_V(name, cfn)   MCS__SIG(name) { MCS__U; cfn(); return mcs_null(); }
#define MCS_WRAP_V_I(name, cfn)   MCS__SIG(name) { MCS__U; long a = (long)MCS__I(0); MCS__RET_IF_EXC(); cfn(a); return mcs_null(); }
#define MCS_WRAP_V_II(name, cfn)  MCS__SIG(name) { MCS__U; long a = (long)MCS__I(0), b = (long)MCS__I(1); MCS__RET_IF_EXC(); cfn(a, b); return mcs_null(); }
#define MCS_WRAP_V_III(name, cfn) MCS__SIG(name) { MCS__U; long a = (long)MCS__I(0), b = (long)MCS__I(1), c = (long)MCS__I(2); MCS__RET_IF_EXC(); cfn(a, b, c); return mcs_null(); }
#define MCS_WRAP_V_IIII(name, cfn) MCS__SIG(name) { MCS__U; long a = (long)MCS__I(0), b = (long)MCS__I(1), c = (long)MCS__I(2), d = (long)MCS__I(3); MCS__RET_IF_EXC(); cfn(a, b, c, d); return mcs_null(); }
#define MCS_WRAP_V_B(name, cfn)   MCS__SIG(name) { MCS__U; bool a = MCS__B(0); MCS__RET_IF_EXC(); cfn(a); return mcs_null(); }
#define MCS_WRAP_V_S(name, cfn)   MCS__SIG(name) { MCS__U; const char* a = MCS__S(0); MCS__RET_IF_EXC(); cfn(a); return mcs_null(); }
#define MCS_WRAP_V_IS(name, cfn)  MCS__SIG(name) { MCS__U; long a = (long)MCS__I(0); const char* b = MCS__S(1); MCS__RET_IF_EXC(); cfn(a, b); return mcs_null(); }
#define MCS_WRAP_V_IIS(name, cfn) MCS__SIG(name) { MCS__U; long a = (long)MCS__I(0), b = (long)MCS__I(1); const char* c = MCS__S(2); MCS__RET_IF_EXC(); cfn(a, b, c); return mcs_null(); }

/* ---- integer results */
#define MCS_WRAP_I_V(name, cfn)   MCS__SIG(name) { MCS__U; return mcs_int((mcs_int_t)cfn()); }
#define MCS_WRAP_I_I(name, cfn)   MCS__SIG(name) { MCS__U; long a = (long)MCS__I(0); MCS__RET_IF_EXC(); return mcs_int((mcs_int_t)cfn(a)); }
#define MCS_WRAP_I_II(name, cfn)  MCS__SIG(name) { MCS__U; long a = (long)MCS__I(0), b = (long)MCS__I(1); MCS__RET_IF_EXC(); return mcs_int((mcs_int_t)cfn(a, b)); }
#define MCS_WRAP_I_III(name, cfn) MCS__SIG(name) { MCS__U; long a = (long)MCS__I(0), b = (long)MCS__I(1), c = (long)MCS__I(2); MCS__RET_IF_EXC(); return mcs_int((mcs_int_t)cfn(a, b, c)); }
#define MCS_WRAP_I_S(name, cfn)   MCS__SIG(name) { MCS__U; const char* a = MCS__S(0); MCS__RET_IF_EXC(); return mcs_int((mcs_int_t)cfn(a)); }
#define MCS_WRAP_I_IS(name, cfn)  MCS__SIG(name) { MCS__U; long a = (long)MCS__I(0); const char* b = MCS__S(1); MCS__RET_IF_EXC(); return mcs_int((mcs_int_t)cfn(a, b)); }

/* ---- bool results */
#define MCS_WRAP_B_V(name, cfn)   MCS__SIG(name) { MCS__U; return mcs_bool(cfn() != 0); }
#define MCS_WRAP_B_I(name, cfn)   MCS__SIG(name) { MCS__U; long a = (long)MCS__I(0); MCS__RET_IF_EXC(); return mcs_bool(cfn(a) != 0); }

/* ---- string results (C string is copied) */
#define MCS_WRAP_S_V(name, cfn)   MCS__SIG(name) { MCS__U; const char* r = cfn(); return r ? mcs_string(vm, r) : mcs_null(); }
#define MCS_WRAP_S_I(name, cfn)   MCS__SIG(name) { MCS__U; long a = (long)MCS__I(0); MCS__RET_IF_EXC(); const char* r = cfn(a); return r ? mcs_string(vm, r) : mcs_null(); }

/* ---- float results */
#if MCS_ENABLE_FLOAT
#define MCS_WRAP_F_V(name, cfn)   MCS__SIG(name) { MCS__U; return mcs_float((mcs_float_t)cfn()); }
#define MCS_WRAP_F_F(name, cfn)   MCS__SIG(name) { MCS__U; double a = (double)MCS__F(0); MCS__RET_IF_EXC(); return mcs_float((mcs_float_t)cfn(a)); }
#define MCS_WRAP_F_FF(name, cfn)  MCS__SIG(name) { MCS__U; double a = (double)MCS__F(0), b = (double)MCS__F(1); MCS__RET_IF_EXC(); return mcs_float((mcs_float_t)cfn(a, b)); }
#define MCS_WRAP_F_I(name, cfn)   MCS__SIG(name) { MCS__U; long a = (long)MCS__I(0); MCS__RET_IF_EXC(); return mcs_float((mcs_float_t)cfn(a)); }
#define MCS_WRAP_V_F(name, cfn)   MCS__SIG(name) { MCS__U; double a = (double)MCS__F(0); MCS__RET_IF_EXC(); cfn(a); return mcs_null(); }
#define MCS_WRAP_V_IF(name, cfn)  MCS__SIG(name) { MCS__U; long a = (long)MCS__I(0); double b = (double)MCS__F(1); MCS__RET_IF_EXC(); cfn(a, b); return mcs_null(); }
#endif

/* ---- constants:  MCS_CONST_INT(vm, "Gpio", "HIGH", 1) */
#define MCS_CONST_INT(vm, mod, name, v) mcs_module_set(vm, mod, name, mcs_int((mcs_int_t)(v)))
#define MCS_CONST_STR(vm, mod, name, s) mcs_module_set(vm, mod, name, mcs_string(vm, s))

#endif

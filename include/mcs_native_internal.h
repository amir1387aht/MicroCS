/* MicroCS internal native-module helpers; not a public embedding API.
 * Kept here so modules do not depend on the VM layout or source-tree paths. */
#ifndef MCS_NATIVE_INTERNAL_H
#define MCS_NATIVE_INTERNAL_H
#include "mcs.h"

/* Catch an allocation panic only long enough to release a native resource.
 * body may update ctx; ownership must live in the caller, outside the setjmp
 * frame. This is NOT a VM execution guard: it does not restore stack/roots or
 * turn a panic into a C# exception. After nonzero return, perform allocation-
 * free cleanup and immediately rethrow to the enclosing VM execution guard. */
int mcs_protect_native(mcs_vm_t* vm, void (*body)(void*), void* ctx);
void mcs_rethrow_native(mcs_vm_t* vm, int code);  /* zero is a no-op */
#endif

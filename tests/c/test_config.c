/* How include/mcs_config.h combines the project config header
 * (mcs_user_config.h), the profile and -D options. Built by `make test` with
 * tests/c/config/mcs_user_config.h found in several ways, plus
 * -DMCS_DEFAULT_PROFILE=MCS_PROFILE_MIN -DMCS_GC_GROW=3 on the command line;
 * EXPECT_NO_HEADER = built with MCS_USER_CONFIG=0. Links against the library
 * built the same way, so a mismatch would also show up as a failing VM run. */
#include "mcs.h"
#include "mcs_shell.h"
#include <stdio.h>

#if MCS_GC_GROW != 3
#error "an option given with -D must win"
#endif

#if defined(EXPECT_NO_HEADER)
#  ifdef MCS_TEST_USER_CONFIG
#    error "MCS_USER_CONFIG=0 must skip mcs_user_config.h"
#  endif
#  if MCS_PROFILE != MCS_PROFILE_MIN || MCS_ENABLE_FLOAT != 0 || MCS_DEFAULT_STACK != 128
#    error "without the header the build's MCS_DEFAULT_PROFILE (min) applies"
#  endif
#  define WHAT "MCS_USER_CONFIG=0: header skipped, build profile min"
#else
#  ifndef MCS_TEST_USER_CONFIG
#    error "mcs_user_config.h was not found"
#  endif
#  if MCS_PROFILE != MCS_PROFILE_LOWRAM
#    error "MCS_PROFILE from the header must win over MCS_DEFAULT_PROFILE"
#  endif
#  if MCS_FLOAT_DOUBLE != 0 || MCS_ENABLE_COMPILER != 0 || MCS_POOL_ALIGN != 4
#    error "the header's profile (lowram) must supply the defaults it does not set"
#  endif
#  if MCS_ENABLE_FLOAT != 1
#    error "the min profile must not be applied"
#  endif
#  if MCS_DEFAULT_STACK != 200 || MCS_ENABLE_LINQ != 0
#    error "options in the header must win over the profile"
#  endif
#  if MCS_SHELL_LINE_MAX != 128
#    error "module options must come from the header too"
#  endif
#  define WHAT "header + lowram profile + -D"
#endif

int main(void) {
    static uint8_t heap[sizeof(void*) == 8 ? 160 * 1024 : 64 * 1024];
    static mcs_pool_t pool;
    mcs_config_t cfg;
    mcs_config_default(&cfg);
    mcs_pool_init(&pool, heap, sizeof heap);
    cfg.realloc_fn = mcs_pool_realloc;
    cfg.alloc_ud = &pool;
    mcs_vm_t* vm = mcs_new(&cfg);
    if (!vm) { puts("FAIL config: mcs_new"); return 1; }
    mcs_free(vm);
    printf("PASS config (%s)\n", WHAT);
    return 0;
}

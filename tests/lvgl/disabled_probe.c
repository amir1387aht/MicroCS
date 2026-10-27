#include "mcs_internal.h"
#include <stdio.h>
#if MCS_ENABLE_LVGL
#error "disabled proof must compile with LVGL off"
#endif
int main(void) {
    mcs_config_t cfg;mcs_config_default(&cfg);mcs_vm_t* vm=mcs_new(&cfg);if(!vm)return 1;
    mcs_mem_stats_t s;mcs_mem_stats(vm,&s);
    printf("vm_struct=%zu ext_slots=%d startup_live=%zu objects=%u\n",sizeof(mcs_vm_t),MCS_EXT__COUNT,s.bytes_in_use,(unsigned)s.objects);
    mcs_free(vm);return 0;
}

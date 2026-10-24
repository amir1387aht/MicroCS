/* Optional MCP2515 omission must not disable native CAN or misreport templates. */
#include "mcs_hal.h"
#include <stdio.h>
int main(void) {
    mcs_config_t cfg; mcs_config_default(&cfg);
    mcs_vm_t* vm = mcs_new(&cfg);
    if (!vm) return 1;
    mcs_hal_t hal; mcs_hal_sim_t sim = {0}; mcs_hal_sim_init(&hal, &sim); mcs_hal_open_lib(vm, &hal);
    int ok = mcs_capability_has(vm, "CAN") && mcs_capability_has(vm, "CAN.Configure") && !mcs_capability_has(vm, "CAN.Mcp2515");
    if (mcs_exec_source(vm, "native_can.cs", "CAN.Open(0, new CanConfig { Mode = CAN.Loopback }); CAN.Send(0, 0x123, new byte[] {42}); var f = CAN.Receive(0); if (f == null || f.Id != 0x123 || f.Data[0] != 42) throw new Exception(); CAN.Close(0);") != MCS_OK) ok = 0;
    mcs_hal_close_lib(vm); mcs_free(vm);
    puts(ok ? "PASS native CAN with MCP2515 disabled" : "FAIL native CAN with MCP2515 disabled");
    return !ok;
}

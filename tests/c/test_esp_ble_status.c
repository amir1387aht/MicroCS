/* Production conversion helper, SDK constants stand-ins like the power test. */
#include <assert.h>
#include "mcs_hal.h"
typedef int esp_err_t;
enum { ESP_OK=0, ESP_FAIL=-1, ESP_ERR_NO_MEM=257, ESP_ERR_INVALID_ARG=258,
    ESP_ERR_INVALID_STATE=259, ESP_ERR_NOT_FOUND=261, ESP_ERR_NOT_SUPPORTED=262, ESP_ERR_TIMEOUT=263 };
#include "mcs_esp32_ble_status.h"
int main(void) {
    assert(mcs_esp32_ble_init_status(ESP_OK)==0);
    assert(mcs_esp32_ble_init_status(ESP_ERR_NO_MEM)==MCS_HAL_ENOMEM);
    assert(mcs_esp32_ble_init_status(ESP_ERR_NO_MEM)!=MCS_HAL_ENODEV);
    assert(mcs_esp32_ble_init_status(ESP_ERR_TIMEOUT)==MCS_HAL_ETIMEOUT);
    assert(mcs_esp32_ble_init_status(ESP_ERR_INVALID_ARG)==MCS_HAL_EINVAL);
    assert(mcs_esp32_ble_init_status(ESP_ERR_INVALID_STATE)==MCS_HAL_EBUSY);
    assert(mcs_esp32_ble_init_status(ESP_ERR_NOT_FOUND)==MCS_HAL_ENODEV);
    assert(mcs_esp32_ble_init_status(ESP_ERR_NOT_SUPPORTED)==MCS_HAL_ENOTSUP);
    assert(mcs_esp32_ble_init_status(ESP_FAIL)==MCS_HAL_ERR);
    assert(mcs_esp32_ble_init_status(0x110d)==MCS_HAL_ERR); /* NVS failure != native OOM */
    return 0;
}

/* Internal helpers; include ESP-IDF error/heap/log headers first. */
#ifndef MCS_ESP32_BLE_DIAGNOSTICS_H
#define MCS_ESP32_BLE_DIAGNOSTICS_H
/* Diagnostics read the INTERNAL heap used by the radio, not VM free space.
 * lifetime_min is the SDK low-water counter, NOT a BLE-only peak measurement. */
#define BLE_MEM_CAPS (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
static void ble_memory_failure(const char* stage, esp_err_t error) {
    size_t free = heap_caps_get_free_size(BLE_MEM_CAPS);
    size_t largest = heap_caps_get_largest_free_block(BLE_MEM_CAPS);
    size_t minimum = heap_caps_get_minimum_free_size(BLE_MEM_CAPS);
    ESP_LOGE("MicroCS-BLE", "%s failed: %s (%d); internal free=%u largest=%u lifetime_min=%u; VM-pool free is not native BLE memory",
        stage, esp_err_to_name(error), (int)error, (unsigned)free, (unsigned)largest, (unsigned)minimum);
}
#if defined(CONFIG_MICROCS_BLE_MEMORY_DIAGNOSTICS) && CONFIG_MICROCS_BLE_MEMORY_DIAGNOSTICS
/* Explicit debug opt-in: SDK has one global failed-allocation callback slot.
 * ROM printing avoids heap allocation/log-buffer allocation from this hook. */
static void ble_allocation_failed(size_t size, uint32_t caps, const char* function_name) {
    esp_rom_printf("MicroCS-BLE: native allocation failed: request=%u caps=0x%08x function=%s\n",
        (unsigned)size, (unsigned)caps, function_name ? function_name : "unknown");
}
static void ble_memory_snapshot(const char* stage) {
    ESP_LOGI("MicroCS-BLE", "%s: internal free=%u largest=%u lifetime_min=%u",
        stage, (unsigned)heap_caps_get_free_size(BLE_MEM_CAPS),
        (unsigned)heap_caps_get_largest_free_block(BLE_MEM_CAPS),
        (unsigned)heap_caps_get_minimum_free_size(BLE_MEM_CAPS));
}
static void ble_memory_debug_enable(void) {
    static int installed;
    if (!installed) {
        esp_err_t e = heap_caps_register_failed_alloc_callback(ble_allocation_failed);
        if (e == ESP_OK) installed = 1;
        else ESP_LOGW("MicroCS-BLE", "cannot install native allocation debug callback: %s", esp_err_to_name(e));
    }
}
#else
#define ble_memory_snapshot(stage) ((void)0)
#define ble_memory_debug_enable() ((void)0)
#endif

#endif

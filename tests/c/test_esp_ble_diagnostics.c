/* Compile the production diagnostics with SDK stand-ins, enabled and disabled. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 257
#define MALLOC_CAP_INTERNAL 2048
#define MALLOC_CAP_8BIT 4
static unsigned samples,registrations,logs,rom_logs;
static esp_err_t registration_error;
static void (*failure_hook)(size_t,uint32_t,const char*);
static char output[512];
static size_t heap_caps_get_free_size(unsigned caps) { assert(caps==2052);samples++;return 25140; }
static size_t heap_caps_get_largest_free_block(unsigned caps) { assert(caps==2052);samples++;return 24064; }
static size_t heap_caps_get_minimum_free_size(unsigned caps) { assert(caps==2052);samples++;return 716; }
static const char* esp_err_to_name(esp_err_t e) { assert(e==ESP_ERR_NO_MEM);return "ESP_ERR_NO_MEM"; }
static void log_message(const char* tag,const char* fmt,...) {
    assert(!strcmp(tag,"MicroCS-BLE"));logs++;
    va_list ap;va_start(ap,fmt);vsnprintf(output,sizeof output,fmt,ap);va_end(ap);
}
#define ESP_LOGI log_message
#define ESP_LOGW log_message
#define ESP_LOGE log_message
#if CONFIG_MICROCS_BLE_MEMORY_DIAGNOSTICS
static esp_err_t heap_caps_register_failed_alloc_callback(void (*fn)(size_t,uint32_t,const char*)) {
    registrations++;if(registration_error)return registration_error;failure_hook=fn;return ESP_OK;
}
static void esp_rom_printf(const char* fmt,...) {
    rom_logs++;va_list ap;va_start(ap,fmt);vsnprintf(output,sizeof output,fmt,ap);va_end(ap);
}
#endif
#include "mcs_esp32_ble_diagnostics.h"
int main(void) {
#if CONFIG_MICROCS_BLE_MEMORY_DIAGNOSTICS
    registration_error=ESP_ERR_NO_MEM;ble_memory_debug_enable();assert(registrations==1 && !failure_hook);
    registration_error=ESP_OK;ble_memory_debug_enable();assert(registrations==2 && failure_hook);
    ble_memory_debug_enable();assert(registrations==2);
    samples=logs=0;ble_memory_snapshot("before nimble_port_init");
    assert(samples==3 && logs==1 && strstr(output,"before nimble_port_init"));
    assert(strstr(output,"free=25140") && strstr(output,"largest=24064") && strstr(output,"lifetime_min=716"));
    unsigned before=samples;failure_hook(4096,2052,"test_allocator");
    assert(rom_logs==1 && samples==before); /* hook neither queries heap nor allocates */
    assert(strstr(output,"request=4096") && strstr(output,"caps=0x00000804") && strstr(output,"test_allocator"));
#else
    ble_memory_debug_enable();ble_memory_snapshot("unused");
    assert(!samples && !logs && !registrations && !rom_logs && !failure_hook && !registration_error);
#endif
    samples=logs=0;ble_memory_failure("nimble_port_init (controller/host)",ESP_ERR_NO_MEM);
    assert(samples==3 && logs==1 && strstr(output,"ESP_ERR_NO_MEM (257)"));
    assert(strstr(output,"VM-pool free is not native BLE memory"));
    return 0;
}

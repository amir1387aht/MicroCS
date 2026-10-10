/*
 * MicroCS port for Zephyr RTOS (3.4 or newer): every board Zephyr supports -
 * Nordic nRF52/nRF53/nRF54, NXP i.MX RT / Kinetis / LPC, STM32, SAM, RP2040,
 * ESP32, Renesas, Silicon Labs ... - through Zephyr's portable driver APIs.
 *
 * Add MicroCS as a Zephyr module (west.yml or ZEPHYR_EXTRA_MODULES), set
 * CONFIG_MICROCS=y, then
 *
 *     #include "mcs_port_zephyr.h"
 *     static mcs_hal_t hal;
 *     mcs_zephyr_hal_init(&hal, NULL);      // devices from the devicetree, see below
 *
 * Devices are found in the devicetree:
 *   GPIO   every `gpio0`, `gpio1` ... or `gpioa`, `gpiob` ... node label.
 *          C# pin = port * 32 + pin; names: "P0.13" (nRF), "PA5" (STM32), "13"
 *   UART   aliases mcs-uart0 .. mcs-uart3 (UART 0 defaults to zephyr,console)
 *   I2C    aliases mcs-i2c0 .. mcs-i2c1   (fallback: node labels i2c0, i2c1)
 *   SPI    aliases mcs-spi0 .. mcs-spi1   (fallback: spi1, spi2)
 *   ADC    `io-channels` of the /zephyr,user node: ADC.Read(n) = n-th entry
 *   PWM    `pwms` of the /zephyr,user node:        PWM.Set(n, ...) = n-th entry
 *   DAC    alias mcs-dac
 *   CAN    aliases mcs-can0 / mcs-can1 (fallback: chosen zephyr,canbus)
 *   I2S    aliases mcs-i2s0 / mcs-i2s1 (fallback: node label i2s0)
 *   Watchdog alias watchdog0 (fallback: wdt0, wdt, iwdg)    RTC  alias rtc (else a software clock)
 *
 * Files: LittleFS on the `storage_partition` flash partition (mcs_zephyr_fs_mount)
 * or any filesystem Zephyr has mounted - FAT on an SD card ("/SD:"), fstab ... -
 * through mcs_zephyr_fs_ops (needs CONFIG_FILE_SYSTEM=y).
 *
 * Port options (MCS_ZEPHYR_UART_RXBUF, _TIMERS, _DAC_BITS, _I2S_BLOCK(S),
 * _FS_FILES, _FS_MOUNT) and every MCS_* option: mcs_user_config.h in the
 * application folder (template: config/mcs_user_config.h), or -D.
 */
#ifndef MCS_PORT_ZEPHYR_H
#define MCS_PORT_ZEPHYR_H
#include "mcs.h"
#include "mcs_hal.h"
#include "mcs_shell.h"
#include "mcs_vfs.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* name;           /* Hal.Board; NULL = CONFIG_BOARD */
} mcs_zephyr_cfg_t;

void mcs_zephyr_hal_init(mcs_hal_t* hal, const mcs_zephyr_cfg_t* cfg);

/* Console on the zephyr,console UART (interrupt-driven when CONFIG_UART_INTERRUPT_DRIVEN=y). */
mcs_transport_t mcs_zephyr_console(void);
uint32_t mcs_zephyr_ticks(void* ud);
void mcs_zephyr_delay(void* ud, uint32_t ms);

#if MCS_ENABLE_FS
#ifndef MCS_ZEPHYR_FS_MOUNT
#define MCS_ZEPHYR_FS_MOUNT "/lfs"
#endif
/* A Zephyr mount point as a MicroCS filesystem: pass &mcs_zephyr_fs_ops and the
 * mcs_zephyr_fs_t as mcs_runtime_cfg_t.fs_ops / fs_ctx. */
typedef struct { char root[32]; const char* format; } mcs_zephyr_fs_t;
extern const mcs_vfs_ops_t mcs_zephyr_fs_ops;
/* Mount LittleFS on `storage_partition` at MCS_ZEPHYR_FS_MOUNT (formats it the
 * first time; reuses an fstab automount) and wrap it. 0 or MCS_VFS_E*. */
int mcs_zephyr_fs_mount(mcs_zephyr_fs_t* fs);
/* Wrap a filesystem that is already mounted, e.g. "/SD:" (FAT) - format is
 * the name `df` / DriveInfo report ("fat", "littlefs" ...). */
int mcs_zephyr_fs_init(mcs_zephyr_fs_t* fs, const char* mount_point, const char* format);
#endif

#if MCS_ENABLE_FLASH && defined(CONFIG_FLASH_MAP)
/* A flash-map partition as a raw mcs_flash_t (erase blocks from the page
 * layout, write_size = the driver's write block) for mcs_flashfs_mount():
 * YAFFS2 (CONFIG_MICROCS_YAFFS2) or MicroCS's own LittleFS on a partition
 * Zephyr's file system layer does not manage.
 *   mcs_zephyr_flash_area_init(&f, FIXED_PARTITION_ID(storage_partition)); */
#include "mcs_flash.h"
typedef struct { mcs_flash_t flash; const void* fa; } mcs_zephyr_flash_t;
int mcs_zephyr_flash_area_init(mcs_zephyr_flash_t* f, int area_id);
#endif

#ifdef __cplusplus
}
#endif
#endif

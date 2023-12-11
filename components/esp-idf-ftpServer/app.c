/*
 FTP Server example.
 This example code is in the Public Domain (or CC0 licensed, at your option.)

 Unless required by applicable law or agreed to in writing, this
 software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_mac.h" // for MACSTR
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs.h"
#include "nvs_flash.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "mdns.h"
#include "esp_sntp.h"

#include "lwip/dns.h"

#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0))
#define esp_vfs_fat_spiflash_mount esp_vfs_fat_spiflash_mount_rw_wl
#define esp_vfs_fat_spiflash_unmount esp_vfs_fat_spiflash_unmount_rw_wl
#define sntp_setoperatingmode esp_sntp_setoperatingmode
#define sntp_setservername esp_sntp_setservername
#define sntp_init esp_sntp_init
#endif


static const char *TAG = "FTP app";
static char *MOUNT_POINT = "/root";

EventGroupHandle_t xEventTask;
int FTP_TASK_FINISH_BIT = BIT2;

#define CONFIG_SPI_SDCARD	1
#define CONFIG_SDSPI_MOSI	15
#define CONFIG_SDSPI_MISO	02
#define CONFIG_SDSPI_CLK	14
#define CONFIG_SDSPI_CS		13



#if CONFIG_SPI_SDCARD || CONFIG_MMC_SDCARD
esp_err_t mountSDCARD(char * mount_point, sdmmc_card_t * card){
	ESP_LOGI(TAG, "Initializing SDCARD file system");
	esp_err_t ret;
	// Options for mounting the filesystem.
	// If format_if_mount_failed is set to true, SD card will be partitioned and
	// formatted in case when mounting fails.
	esp_vfs_fat_sdmmc_mount_config_t mount_config = {
		.format_if_mount_failed = true,
		.max_files = 4, // maximum number of files which can be open at the same time
		.allocation_unit_size = 16 * 1024
	};
	// Use settings defined above to initialize SD card and mount FAT filesystem.
	// Note: esp_vfs_fat_sdmmc/sdspi_mount is all-in-one convenience functions.
	// Please check its source code and implement error recovery when developing
	// production applications.
	ESP_LOGI(TAG, "Using SPI peripheral");
	ESP_LOGI(TAG, "SDSPI_MOSI=%d", CONFIG_SDSPI_MOSI);
	ESP_LOGI(TAG, "SDSPI_MISO=%d", CONFIG_SDSPI_MISO);
	ESP_LOGI(TAG, "SDSPI_CLK=%d", CONFIG_SDSPI_CLK);
	ESP_LOGI(TAG, "SDSPI_CS=%d", CONFIG_SDSPI_CS);

	sdmmc_host_t host = SDSPI_HOST_DEFAULT();
	spi_bus_config_t bus_cfg = {
		.mosi_io_num = CONFIG_SDSPI_MOSI,
		.miso_io_num = CONFIG_SDSPI_MISO,
		.sclk_io_num = CONFIG_SDSPI_CLK,
		.quadwp_io_num = -1,
		.quadhd_io_num = -1,
		.max_transfer_sz = 4000,
	};
	ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to initialize bus.");
		return ret;
	}
	// This initializes the slot without card detect (CD) and write protect (WP) signals.
	// Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
	sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
	slot_config.gpio_cs = CONFIG_SDSPI_CS;
	slot_config.host_id = host.slot;

	ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);
	ESP_LOGI(TAG, "esp_vfs_fat_sdspi_mount=%d", ret);

	if (ret != ESP_OK) {
		if (ret == ESP_FAIL) {
			ESP_LOGE(TAG, "Failed to mount filesystem. "
				"If you want the card to be formatted, set format_if_mount_failed = true.");
		} else {
			ESP_LOGE(TAG, "Failed to initialize the card (%s). "
				"Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
		}
		return ret;
	}

	// Card has been initialized, print its properties
	sdmmc_card_print_info(stdout, card);
	ESP_LOGI(TAG, "Mounte SD card on %s", mount_point);
	return ret;
}
#endif 

void ftp_task (void *pvParameters);

void ftp_app_main(void)
{
	// Initialize NVS 
	
	esp_err_t ret;
	/*ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);*/


#if CONFIG_SPI_SDCARD
	if (CONFIG_SDSPI_POWER != -1) {
		//gpio_pad_select_gpio(CONFIG_SDSPI_POWER);
		gpio_reset_pin(CONFIG_SDSPI_POWER);
		/* Set the GPIO as a push/pull output */
		gpio_set_direction(CONFIG_SDSPI_POWER, GPIO_MODE_OUTPUT);
		ESP_LOGI(TAG, "Turning on the peripherals power using GPIO%d", CONFIG_SDSPI_POWER);
		gpio_set_level(CONFIG_SDSPI_POWER, 1);
		vTaskDelay(3000 / portTICK_PERIOD_MS);
	}
#endif
	
#if CONFIG_SPI_SDCARD
	// Mount FAT File System on SDCARD
	sdmmc_card_t card;
	ret = mountSDCARD(MOUNT_POINT, &card);
	if (ret != ESP_OK) {
		while(1) { vTaskDelay(1); }
	};
#endif 

	// Create FTP server task
	xEventTask = xEventGroupCreate();
	xTaskCreate(ftp_task, "FTP", 1024*6, NULL, 2, NULL);
	xEventGroupWaitBits( xEventTask,
		FTP_TASK_FINISH_BIT, /* The bits within the event group to wait for. */
		pdTRUE, /* BIT_0 should be cleared before returning. */
		pdFALSE, /* Don't wait for both bits, either bit will do. */
		portMAX_DELAY);/* Wait forever. */
	ESP_LOGE(TAG, "ftp_task finish");

#if CONFIG_SPI_SDCARD
	esp_vfs_fat_sdcard_unmount(MOUNT_POINT, &card);
	ESP_LOGI(TAG, "SDCARD unmounted");
#endif 

}

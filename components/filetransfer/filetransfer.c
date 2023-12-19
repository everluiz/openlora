
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include <esp_log.h>

#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "filetransfer.h"


#define CONFIG_SPI_SDCARD  1
#define CONFIG_SDSPI_MOSI	15
#define CONFIG_SDSPI_MISO	02
#define CONFIG_SDSPI_CLK	14
#define CONFIG_SDSPI_CS		13

static const char *TAG = "FileT";
static char *MOUNT_POINT = "/sdcard";

static file_T_data_t file_data = {0};

EventGroupHandle_t xEventTask;
int TASK_FINISH_BIT = BIT2;

#if CONFIG_SPI_SDCARD
esp_err_t mountSDCARD(char * mount_point, sdmmc_card_t * card) {
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
	//sdmmc_card_t* card;


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
	ESP_LOGI(TAG, "Mounted SD card on %s", mount_point);
	return ret;
}
#endif // CONFIG_SPI_SDCARD


// ==== File functions =========================================

//--------------------------------------------------------------
static bool open_file (const char *path, const char *mode) {
	ESP_LOGI(TAG, "open_file: path=[%s]", path);
	char fullname[128];
	strcpy(fullname, MOUNT_POINT);
	strcat(fullname, path);
	strcat(file_data.filename, path);
	ESP_LOGI(TAG, "open_file: fullname=[%s]", fullname);
	file_data.fp = fopen(fullname, mode);
	if (file_data.fp == NULL) {
		ESP_LOGE(TAG, "open_file: open fail [%s]", fullname);
		return false;
	}
	return true;
}

//---------------------------------------------------------------------------------------------
static file_T_result_t read_file (char *filebuf, uint32_t desiredsize, uint32_t *actualsize) {
	file_T_result_t result = FILE_RESULT_CONTINUE;
	*actualsize = fread(filebuf, 1, desiredsize, file_data.fp);
	if (*actualsize == 0) {
		result = FILE_RESULT_FAILED;
	} else if (*actualsize < desiredsize) {
		result = FILE_RESULT_OK;
	}
	return result;
}

//-----------------------------------------------------------------
static int write_file (char *filebuf, uint32_t size) {
	int result = false;
	uint32_t actualsize = fwrite(filebuf, 1, size, file_data.fp);
	if (actualsize == size) {
		result = true;
	}
	return result;
}

//-----------------------------------------------
static void send_file_data(uint32_t datasize)
{
	int result;
	uint32_t timeout = 200;

	ESP_LOGI(TAG, "Send file data: (%"PRIu32")", datasize);

	while (1) {
		
		result = ol_transp_send(&file_data.client, file_data.buffer, datasize, 0);//todo: delay
		if (result == datasize) {
			//vTaskDelay(1);
			ESP_LOGI(TAG, "Send OK");
			break;
		}
		else {
			//vTaskDelay(1);
			if (timeout <= 0){
				// error
				//_ftp_reset();
				ESP_LOGW(TAG, "Error sending file data.");
				break;
			}
		}
		timeout -= portTICK_PERIOD_MS;
	}
}


void filetransfer_task(void *pvParameters){
	file_T_header_t header;
	file_T_trailer_t trailer;
	header.file_id = 1;
	header.seq_number = 0;
	trailer.sha = 1;
	char buffer[OL_FILETRANSFER_MAX_PAYLOAD_SIZE];
	int readsize;
	int result = FILE_RESULT_CONTINUE;

	// send first package with file_id and filename in the payload.
	strcpy(buffer, file_data.filename);
	// Allocate the payload size
	uint8_t *payload = pvPortMalloc(strlen(buffer)+ sizeof(file_T_header_t)+ sizeof(file_T_trailer_t));
	memcpy(payload, &header, sizeof(file_T_header_t));
	memcpy(payload+sizeof(file_T_header_t), &buffer, strlen(buffer));
	memcpy(payload+sizeof(file_T_header_t)+strlen(buffer), &trailer, sizeof(file_T_trailer_t));
	ESP_LOGI(TAG, "Size: %d - %s", strlen(buffer), payload+sizeof(file_T_header_t));
	int sent = ol_transp_send(&file_data.client, payload, strlen(buffer)+ sizeof(file_T_header_t)+ sizeof(file_T_trailer_t), portMAX_DELAY);
	header.seq_number = 1;

	while(result == FILE_RESULT_CONTINUE){
		memset(buffer, "", sizeof(buffer));
		result = read_file ((char *) &buffer, OL_FILETRANSFER_MAX_PAYLOAD_SIZE, (uint32_t*) &readsize);
		if (result == FILE_RESULT_FAILED){
			ESP_LOGI(TAG, "file size zero.");
			// sinalizar que arquivo está vazio ou terminou de enviar
		}else{
			if (result == FILE_RESULT_CONTINUE){
				trailer.sha = 1;
				//ESP_LOGI(TAG, "Size: %d - %s", readsize, buffer);
				// arquivo maior que 1 payload, continua enviando

				//file_T_header.payload_size = OL_FILETRANSFER_SIZE;
				payload = pvPortMalloc(OL_FILETRANSFER_SIZE);
				memcpy(payload, &header, sizeof(file_T_header_t));
				memcpy(payload+sizeof(file_T_header_t), &buffer, OL_FILETRANSFER_MAX_PAYLOAD_SIZE);
				memcpy(payload+sizeof(file_T_header_t)+OL_FILETRANSFER_MAX_PAYLOAD_SIZE, &trailer, sizeof(file_T_trailer_t));
				ESP_LOGI(TAG, "Size: %d - %s", OL_FILETRANSFER_SIZE, payload+sizeof(file_T_header_t));
				int sent = ol_transp_send(&file_data.client, payload, OL_FILETRANSFER_SIZE, portMAX_DELAY);
				if (sent == OL_FILETRANSFER_SIZE) {
					header.seq_number++;
					// Envio realizado ,continua enviando
					//ESP_LOGI(TAG, "enviando..");
				}
				vTaskDelay(1);
			}
			if (result == FILE_RESULT_OK) {
				trailer.sha = 1;

				payload = pvPortMalloc(readsize+ sizeof(file_T_header_t)+ sizeof(file_T_trailer_t));
				memcpy(payload, &header, sizeof(file_T_header_t));
				memcpy(payload+sizeof(file_T_header_t), &buffer, readsize);
				memcpy(payload+sizeof(file_T_header_t)+OL_FILETRANSFER_MAX_PAYLOAD_SIZE, &trailer, sizeof(file_T_trailer_t));

				int sent = ol_transp_send(&file_data.client,(uint8_t*) buffer, readsize+ sizeof(file_T_header_t)+ sizeof(file_T_trailer_t), portMAX_DELAY);
				if (sent == readsize+ sizeof(file_T_header_t)+ sizeof(file_T_trailer_t)) {
					// Envio terminado.
					ESP_LOGI(TAG, "Size: %d - %s", sent, payload+sizeof(file_T_header_t));
				}
				
			}
		}
	}
	xEventGroupSetBits(xEventTask, TASK_FINISH_BIT);
	vTaskDelete(NULL);
}

BaseType_t filetransfer(char * filepath){
	BaseType_t result;
	file_data.client.protocol = TRANSP_STREAM;
	file_data.client.src_port = OL_TRANSPORT_CLIENT_PORT_INIT+1;
	file_data.client.dst_port = 1;
    file_data.client.dst_addr = OL_BORDER_ROUTER_ADDR;
	ol_transp_open(&file_data.client);

	esp_err_t ret;

	#if CONFIG_SPI_SDCARD
	// Mount FAT File System on SDCARD
	sdmmc_card_t card;
	ret = mountSDCARD(MOUNT_POINT, &card);
	while(ret != ESP_OK) { }
	#endif

	if(open_file(filepath,"rb")){
		xEventTask = xEventGroupCreate();
		xTaskCreate(filetransfer_task, "filetransfer", 1024*6, NULL, 2, NULL);
		xEventGroupWaitBits( xEventTask,
		TASK_FINISH_BIT, /* The bits within the event group to wait for. */
		pdTRUE, /* BIT_0 should be cleared before returning. */
		pdFALSE, /* Don't wait for both bits, either bit will do. */
		portMAX_DELAY);/* Wait forever. */
		ESP_LOGE(TAG, "task finish");

		fclose(file_data.fp);
		file_data.fp = NULL;
		result = pdPASS;	
	}else{
		result = pdFAIL;
	}

	#if CONFIG_SPI_SDCARD
	esp_vfs_fat_sdcard_unmount(MOUNT_POINT, &card);
	ESP_LOGI(TAG, "SDCARD unmounted");
	#endif

	return result; 
}





BaseType_t filereceive(char * filepath){
	BaseType_t result;
	file_data.client.protocol = TRANSP_STREAM;
	file_data.client.src_port = OL_TRANSPORT_CLIENT_PORT_INIT+1;
	file_data.client.dst_port = 1;
    file_data.client.dst_addr = OL_BORDER_ROUTER_ADDR;
	ol_transp_open(&file_data.client);

	esp_err_t ret;

	#if CONFIG_SPI_SDCARD
	// Mount FAT File System on SDCARD
	sdmmc_card_t card;
	ret = mountSDCARD(MOUNT_POINT, &card);
	while(ret != ESP_OK) { }
	#endif

	if(open_file(filepath,"rb")){
		xEventTask = xEventGroupCreate();
		xTaskCreate(filetransfer_task, "filetransfer", 1024*6, NULL, 2, NULL);
		xEventGroupWaitBits( xEventTask,
		TASK_FINISH_BIT, /* The bits within the event group to wait for. */
		pdTRUE, /* BIT_0 should be cleared before returning. */
		pdFALSE, /* Don't wait for both bits, either bit will do. */
		portMAX_DELAY);/* Wait forever. */
		ESP_LOGE(TAG, "task finish");

		fclose(file_data.fp);
		file_data.fp = NULL;
		result = pdPASS;	
	}else{
		result = pdFAIL;
	}

	#if CONFIG_SPI_SDCARD
	esp_vfs_fat_sdcard_unmount(MOUNT_POINT, &card);
	ESP_LOGI(TAG, "SDCARD unmounted");
	#endif

	return result; 
}

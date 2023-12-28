
#ifndef _FILETRANSFER_H_
#define _FILETRANSFER_H_


#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "openlora.h"

#define OL_FILETRANSFER_SIZE                216
#define OL_FILETRANSFER_MAX_PAYLOAD_SIZE    (OL_FILETRANSFER_SIZE - sizeof(file_T_header_t))
#define FILE_TRANSFER__PORT                 21

typedef struct {
    char                filename[128];  // filename
    uint8_t             file_id;        // file identification
    uint16_t            file_size;      // file size in bytes
    uint16_t            seq_number;     // sequence number
    FILE                *fp;            // file pointer
    transport_layer_t   client;         //  transport layer handler
} file_T_data_t;

typedef struct __attribute__((packed)) {
    uint8_t             file_id;
    uint16_t             file_size;
    uint16_t            seq_number;
}file_T_header_t;

typedef enum {
    FILE_RESULT_OK = 0,
    FILE_RESULT_CONTINUE,
    FILE_RESULT_FAILED
} file_T_result_t;

BaseType_t filetransfer(char* filepath);
BaseType_t filereceive();

#endif /* _FILETRANSFER_H_ */
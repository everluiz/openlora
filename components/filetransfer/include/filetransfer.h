
#ifndef _FILETRANSFER_H_
#define _FILETRANSFER_H_


#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "openlora.h"

#define OL_FILETRANSFER_SIZE    216
#define OL_FILETRANSFER_MAX_PAYLOAD_SIZE    (OL_FILETRANSFER_SIZE - sizeof(file_T_header_t)- sizeof(file_T_trailer_t))
#define FILE_TRANSFER__PORT                 21

typedef struct {
    char                filename[128];// filename
    uint8_t             *buffer;    // data buffer
    FILE                *fp;        // file pointer
    transport_layer_t   client;     //  transport layer handler
} file_T_data_t;

typedef struct __attribute__((packed)) {
    uint8_t             file_id;
    //uint8_t             payload_size;
    uint16_t            seq_number;
}file_T_header_t;

typedef struct __attribute__((packed)) {
    uint16_t            sha;
}file_T_trailer_t;

typedef enum {
    FILE_RESULT_OK = 0,
    FILE_RESULT_CONTINUE,
    FILE_RESULT_FAILED
} file_T_result_t;

BaseType_t filetransfer();

#endif /* _FILETRANSFER_H_ */
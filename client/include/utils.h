#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>

#define SERVER_IP "127.0.0.1"
#define PORT 8080
#define MAX_PAYLOAD 8192

// [네트워크 패킷 헤더 구조체] - 서버와 동일해야 함
typedef struct { 
    uint32_t client_id; 
    uint32_t code; 
    uint32_t length; 
} NetHeader;

// [유틸리티 함수]
void clear_input_buffer(void);
void remove_newline(char* str);

#endif // UTILS_H
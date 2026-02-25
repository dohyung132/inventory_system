#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <netinet/in.h>

// [기본 설정 상수]
#define PORT 8080
#define MAX_PAYLOAD 8192

// [네트워크 패킷 헤더 구조체]
typedef struct { 
    uint32_t client_id; 
    uint32_t code; 
    uint32_t length; 
} NetHeader;

// [클라이언트 소켓 정보 구조체]
typedef struct {
    int sock;
    struct sockaddr_in addr;
} ClientInfo;

// [서버 설정 및 상태 제어 API]
void init_config(int mode);
void load_config(void);
void save_config(void);

int get_server_mode(void);
int get_speed_factor(void);
void set_speed_factor(int new_speed);

int is_clock_showing(void);
void set_clock_showing(int show);

// [시간 및 유틸리티 API]
time_t get_virtual_time(void);
void reset_virtual_time(void);
void print_time_str(time_t t, char* buf);

#endif // UTILS_H
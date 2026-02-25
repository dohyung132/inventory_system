#ifndef NETWORK_H
#define NETWORK_H

#include <stddef.h>
#include <sys/types.h>
#include "utils.h"

// 패킷 송수신 래퍼
ssize_t send_exact(int sock, const void *buf, size_t len);
ssize_t recv_exact(int sock, void *buf, size_t len);

// 클라이언트 핸들러 스레드
void* client_handler(void* arg);

#endif // NETWORK_H
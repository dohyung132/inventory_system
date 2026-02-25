#ifndef NETWORK_H
#define NETWORK_H

#include <stdint.h>
#include "utils.h"

int connect_to_server(const char* ip, int port);
void disconnect_from_server(int sock);

int send_request(int sock, uint32_t cid, uint32_t cmd, const char* payload);
int receive_response(int sock, char* out_payload, int* out_page);

// 중복 코드 제거를 위한 통합 헬퍼 함수
int send_and_receive(int sock, uint32_t cid, uint32_t cmd, const char* payload, char* out_msg, int* out_page);

#endif // NETWORK_H
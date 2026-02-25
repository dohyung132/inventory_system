#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "network.h"

/**
 * @brief 지정한 바이트 수만큼 데이터를 확실하게 전송합니다.
 * [핵심 로직] TCP는 스트림 방식이므로 한 번의 send가 전체 데이터를 보장하지 않습니다.
 * 따라서 루프를 돌며 요청한 len만큼 모든 바이트가 전송될 때까지 반복 시도합니다.
 * * @param sock 연결된 소켓 디스크립터
 * @param buf 전송할 데이터 버퍼
 * @param len 전송할 총 바이트 수
 * @return ssize_t 전송 성공 시 총 바이트 수, 실패 시 -1
 */
static ssize_t send_exact(int sock, const void *buf, size_t len) {
    size_t total = 0;
    const char *p = (const char *)buf;

    while (total < len) {
        /* [보안 및 안정성] MSG_NOSIGNAL 플래그 사용
         * 데이터를 보내는 도중 서버가 연결을 끊으면 클라이언트 프로세스가 SIGPIPE 신호를 받고 
         * 강제 종료될 수 있습니다. 이를 방지하고 안전하게 에러 코드(-1)를 받기 위해 사용합니다.
         */
        ssize_t n = send(sock, p + total, len - total, MSG_NOSIGNAL);
        if (n <= 0) return -1; // 소켓 단절 시 즉시 중단
        total += n;
    }
    return total;
}

/**
 * @brief 지정한 바이트 수만큼 데이터를 확실하게 수신합니다.
 * [핵심 로직] 패킷이 쪼개져서 도착하더라도 헤더 또는 본문의 크기만큼 끝까지 읽어들입니다.
 * * @param sock 연결된 소켓 디스크립터
 * @param buf 데이터를 저장할 버퍼
 * @param len 수신해야 할 총 바이트 수
 * @return ssize_t 수신 성공 시 총 바이트 수, 실패 시 -1
 */
static ssize_t recv_exact(int sock, void *buf, size_t len) {
    size_t total = 0;
    char *p = (char *)buf;

    while (total < len) {
        ssize_t n = recv(sock, p + total, len - total, 0);
        if (n <= 0) return -1; // 서버가 연결을 종료하거나 에러 발생 시 -1 반환
        total += n;
    }
    return total;
}

/**
 * @brief 지정된 IP와 포트를 통해 서버에 TCP 연결을 수행합니다.
 * * @param ip 서버 주소 (문자열 형태)
 * @param port 서버 포트 번호
 * @return int 성공 시 소켓 디스크립터, 실패 시 -1
 */
int connect_to_server(const char* ip, int port) {
    // 1. AF_INET(IPv4), SOCK_STREAM(TCP) 소켓 생성
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    // 2. 서버 주소 구조체 설정
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port); // 호스트 바이트 순서를 네트워크 바이트 순서로 변환

    // 3. 문자열 IP 주소를 이진 주소 값으로 변환
    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
        close(sock);
        return -1;
    }

    // 4. 서버에 3-Way Handshake 연결 요청
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        close(sock);
        return -1;
    }
    return sock;
}

void disconnect_from_server(int sock) {
    close(sock);
}

/**
 * @brief 서버 프로토콜에 맞춘 요청 패킷(Header + Payload) 전송
 * [데이터 처리] 구조체 멤버들을 htonl()로 변환하여 엔디안(Endian) 문제를 해결합니다.
 */
int send_request(int sock, uint32_t cid, uint32_t cmd, const char* payload) {
    NetHeader req;
    
    // 호스트 시스템의 엔디안과 상관없이 빅 엔디안(네트워크 표준)으로 변환
    req.client_id = htonl(cid); 
    req.code = htonl(cmd);
    
    uint32_t len = payload ? (uint32_t)strlen(payload) : 0;
    req.length = htonl(len);

    // [단계 1] 고정 크기의 헤더(12바이트) 먼저 전송
    if (send_exact(sock, &req, sizeof(NetHeader)) < 0) return -1;
    
    // [단계 2] 가변 크기의 실제 데이터(Payload) 전송
    if (len > 0) {
        if (send_exact(sock, payload, len) < 0) return -1;
    }
    return 0;
}

/**
 * @brief 서버 응답 패킷 수신 및 메시지 파싱
 * [단계 1] 응답 헤더를 먼저 읽어 데이터의 길이를 파악합니다.
 * [단계 2] 파악된 길이만큼 본문을 읽고, 구분자('|')를 기준으로 페이지와 메시지를 분리합니다.
 */
int receive_response(int sock, char* out_payload, int* out_page) {
    NetHeader res;
    
    // 헤더 수신 시도 (네트워크 장애 감지의 첫 지점)
    if (recv_exact(sock, &res, sizeof(NetHeader)) <= 0) return -1;

    // 수신한 길이를 네트워크 바이트 순서에서 호스트 순서로 복원 (ntohl)
    uint32_t len = ntohl(res.length);
    if (len >= MAX_PAYLOAD) len = MAX_PAYLOAD - 1; // 버퍼 오버플로우 방지

    char buffer[MAX_PAYLOAD + 512];
    if (len > 0) {
        // 실제 데이터 본문 수신
        if (recv_exact(sock, buffer, len) <= 0) return -1;
    }
    buffer[len] = '\0'; // 문자열 안전성 확보

    // [파싱 로직] "페이지|메시지" 형태의 문자열 분리 (Thread-safe한 strtok_r 사용)
    char *saveptr;
    char *p_str = strtok_r(buffer, "|", &saveptr);
    char *msg = strtok_r(NULL, "", &saveptr);

    // 결과값 출력 매개변수에 할당
    if (out_page && p_str) *out_page = atoi(p_str);
    if (out_payload && msg) strcpy(out_payload, msg);
    else if (out_payload && p_str && !msg) strcpy(out_payload, p_str);

    return 0;
}

/**
 * @brief [통합 래퍼 함수] 요청 전송과 응답 수신을 한 번에 처리
 * 리팩토링 포인트: 코드 중복을 제거하고 비즈니스 로직(pos.c)을 간결하게 만듭니다.
 */
int send_and_receive(int sock, uint32_t cid, uint32_t cmd, const char* payload, char* out_msg, int* out_page) {
    if (send_request(sock, cid, cmd, payload) < 0) return -1;
    return receive_response(sock, out_msg, out_page);
}
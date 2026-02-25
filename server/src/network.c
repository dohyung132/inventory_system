#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "network.h"
#include "inventory.h"
#include "logger.h"

ssize_t send_exact(int sock, const void *buf, size_t len) {
    size_t total = 0; const char *p = (const char *)buf;
    while (total < len) {
        ssize_t n = send(sock, p + total, len - total, 0);
        if (n <= 0) return -1;
        total += n;
    }
    return total;
}

ssize_t recv_exact(int sock, void *buf, size_t len) {
    size_t total = 0; char *p = (char *)buf;
    while (total < len) {
        ssize_t n = recv(sock, p + total, len - total, 0);
        if (n <= 0) return -1;
        total += n;
    }
    return total;
}

void* client_handler(void* arg) {
    ClientInfo* info = (ClientInfo*)arg;
    int sock = info->sock;
    struct sockaddr_in addr = info->addr;
    free(info);

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(addr.sin_addr), client_ip, INET_ADDRSTRLEN);
    
    NetHeader req, res;
    char pin[MAX_PAYLOAD], pout[MAX_PAYLOAD + 512], msg[MAX_PAYLOAD + 256];

    while (recv_exact(sock, &req, sizeof(NetHeader)) > 0) {
        uint32_t cid = ntohl(req.client_id); 
        uint32_t cmd = ntohl(req.code);
        uint32_t len = ntohl(req.length);
        
        size_t rlen = (len < MAX_PAYLOAD - 1) ? len : MAX_PAYLOAD - 1;
        if (rlen > 0) recv_exact(sock, pin, rlen);
        pin[rlen] = '\0'; 
        msg[0] = '\0';
        int out_p = 0;

        // 비즈니스 로직은 이제 inventory.c 가 알아서 처리하고 msg에 결과만 적어줍니다.
        switch(cmd) {
            case 99: 
                snprintf(msg, sizeof(msg), "[접속] 단말기 [POS-%04d] 실행됨 (IP: %s)", cid, client_ip);
                update_log(msg); break;
            case 100: 
                snprintf(msg, sizeof(msg), "[종료] 단말기 [POS-%04d] 종료됨", cid);
                update_log(msg); break;
            case 1: handle_single_import(cid, pin, msg); break;
            case 2: handle_random_import(cid, pin, msg); break;
            case 7: make_category_summary(msg, 0, "전체 재고 요약"); break;
            case 10: make_category_summary(msg, 1, "만료 재고 요약"); break;
            case 15: make_category_summary(msg, 2, "판매 가능 메뉴판"); break;
            case 9: case 11: {
                char *saveptr;
                char *n = strtok_r(pin, "|", &saveptr); 
                char *p = strtok_r(NULL, "|", &saveptr); 
                if(n && p) out_p = make_detail_page(msg, n, atoi(p), (cmd == 11 ? 1 : 0));
                break;
            }
            case 14: handle_sell(cid, pin, msg); break;
            case 16: 
                clear_inventory_db();
                snprintf(msg, sizeof(msg), "[POS-%04d] 창고 비움", cid); 
                update_log(msg); break;
            case 17: handle_cart_verify(pin, msg); break;
            case 5: case 6: case 8: case 12: case 13: 
                handle_delete_operations(cmd, cid, pin, msg); break;
            default:
                snprintf(msg, sizeof(msg), "[오류] 알 수 없는 명령어"); break;
        } 

        snprintf(pout, sizeof(pout), "%d|%.8100s", out_p, msg);
        res.code = htonl(200); res.length = htonl(strlen(pout));
        send_exact(sock, &res, sizeof(NetHeader));
        send_exact(sock, pout, strlen(pout));
    }

    close(sock); 
    return NULL;
}
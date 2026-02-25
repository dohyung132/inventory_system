#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "utils.h"
#include "inventory.h"
#include "logger.h"
#include "network.h"

void handle_sigint(int sig) {
    (void)sig;
    printf("\033[?25h"); 
    printf("\n\n[System] 데이터 저장 및 서버 종료 중...\n");
    save_data(); 
    free_all_resources(); 
    exit(0);
}

void* monitor_thread(void* arg) {
    (void)arg;
    while(1) {
        time_t vt = get_virtual_time();
        char time_str[26]; print_time_str(vt, time_str);

        draw_dashboard(time_str);
        check_and_update_expirations(vt);
        
        usleep(500000); 
    }
    return NULL;
}

int main() {
    printf("\033[2J\033[1;1H");
    printf("======================================\n");
    printf("    스마트 재고 관리 서버 (Server)    \n");
    printf("======================================\n");
    printf("1. 운영 모드 (Operation)\n");
    printf("2. 시뮬레이션 모드 (Simulation)\n");
    printf("--------------------------------------\n");
    printf("선택 >> ");
    
    int mode = 1;
    if (scanf("%d", &mode) != 1) mode = 1;
    getchar(); 

    // 모듈 초기화
    init_config(mode);
    init_inventory();

    // 데이터 복구
    load_persistent_logs(); 
    load_data(); 
    load_config(); 

    // 중단되었던 동안 발생한 만료 처리
    recover_missed_expirations(get_virtual_time());

    printf("\033[2J\033[1;1H"); 
    signal(SIGINT, handle_sigint);

    // 백그라운드 스레드 시작
    pthread_t m_tid, a_tid;
    pthread_create(&m_tid, NULL, monitor_thread, NULL);
    pthread_create(&a_tid, NULL, admin_console_thread, NULL);

    // 서버 소켓 준비
    int s_sock, c_sock; 
    struct sockaddr_in s_addr, c_addr; 
    socklen_t len = sizeof(c_addr);
    
    s_sock = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1; 
    setsockopt(s_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    s_addr.sin_family = AF_INET; 
    s_addr.sin_addr.s_addr = INADDR_ANY; 
    s_addr.sin_port = htons(PORT);
    
    bind(s_sock, (struct sockaddr *)&s_addr, sizeof(s_addr));
    listen(s_sock, 10);

    // 연결 수락 무한 루프
    while((c_sock = accept(s_sock, (struct sockaddr *)&c_addr, &len)) >= 0) {
        ClientInfo* info = malloc(sizeof(ClientInfo));
        if(info) { 
            info->sock = c_sock; 
            info->addr = c_addr;
            pthread_t c_tid; 
            pthread_create(&c_tid, NULL, client_handler, info); 
            pthread_detach(c_tid); 
        }
    }
    return 0;
}
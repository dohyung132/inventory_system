#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "logger.h"
#include "utils.h"
#include "inventory.h"

#define MAX_HISTORY 1000      
#define DASHBOARD_LOGS 15     

// utils.c에 선언된 전역 파일명 가져오기
extern char log_filename[50];
extern char db_filename[50];

// [내부 상태 캡슐화]
static char log_history[MAX_HISTORY][1024]; 
static int log_head = 0; 
static int total_logs = 0;
static char last_log[1024] = "서버 대기 중...";
static int is_browsing_log = 0; 

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER; 
static pthread_mutex_t screen_mutex = PTHREAD_MUTEX_INITIALIZER; 

extern void handle_sigint(int sig); // main.c의 종료 함수 호출용

void update_log(const char* msg) {
    time_t vt = get_virtual_time(); 
    struct tm tm_info; 
    localtime_r(&vt, &tm_info);
    
    char t_str[32]; 
    strftime(t_str, sizeof(t_str), "%Y-%m-%d %H:%M:%S", &tm_info);
    
    char formatted_msg[1024];
    snprintf(formatted_msg, sizeof(formatted_msg), "[%s] %.800s", t_str, msg); 

    pthread_mutex_lock(&log_mutex);
    
    FILE *fp = fopen(log_filename, "a");
    if (fp) { 
        fprintf(fp, "%s\n", formatted_msg); 
        fclose(fp); 
    }

    strncpy(last_log, formatted_msg, sizeof(last_log) - 1);
    strncpy(log_history[log_head], formatted_msg, sizeof(log_history[0]) - 1);
    log_head = (log_head + 1) % MAX_HISTORY;
    total_logs++;
    
    pthread_mutex_unlock(&log_mutex);
}

void load_persistent_logs(void) {
    FILE *fp = fopen(log_filename, "r");
    if (!fp) return;

    char line[1024];
    pthread_mutex_lock(&log_mutex);
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = 0;
        strncpy(log_history[log_head], line, sizeof(log_history[0]) - 1);
        log_head = (log_head + 1) % MAX_HISTORY;
        total_logs++;
        strncpy(last_log, line, sizeof(last_log) - 1); 
    }
    pthread_mutex_unlock(&log_mutex);
    fclose(fp);
}

void clear_persistent_logs(void) {
    pthread_mutex_lock(&log_mutex);
    FILE *fp = fopen(log_filename, "w"); 
    if (fp) fclose(fp);
    
    for(int i = 0; i < MAX_HISTORY; i++) strcpy(log_history[i], "");
    log_head = 0;
    total_logs = 0;
    strcpy(last_log, "로그 초기화됨.");
    pthread_mutex_unlock(&log_mutex);
    
    update_log("[Clear] 로그 파일 및 내역이 초기화되었습니다.");
}

void draw_dashboard(const char* time_str) {
    if (!is_clock_showing() || is_browsing_log) return; 

    pthread_mutex_lock(&screen_mutex); 
    printf("\033[s"); 

    printf("\033[1;1H\033[2K========================================================================");
    if (get_server_mode() == 2)
        printf("\033[2;1H\033[2K [SIMULATION] 배속: x%-5d | DB: %-20s", get_speed_factor(), db_filename);
    else
        printf("\033[2;1H\033[2K [OPERATION] 실시간 동작 (1배속) | DB: %-20s", db_filename);
    
    printf("\033[3;1H\033[2K========================================================================");
    printf("\033[4;1H\033[2K [Time] %s", time_str);
    printf("\033[5;1H\033[2K------------------------------------------------------------------------");

    pthread_mutex_lock(&log_mutex);
    int count = (total_logs < DASHBOARD_LOGS) ? total_logs : DASHBOARD_LOGS;

    for (int i = 0; i < DASHBOARD_LOGS; i++) {
        if (i < count) {
            int idx = (log_head - count + i + MAX_HISTORY) % MAX_HISTORY;
            printf("\033[%d;1H\033[2K %s", 6 + i, log_history[idx]); 
        } else {
            printf("\033[%d;1H\033[2K", 6 + i); 
        }
    }
    pthread_mutex_unlock(&log_mutex);

    printf("\033[%d;1H\033[2K------------------------------------------------------------------------", 6 + DASHBOARD_LOGS);

    if (get_server_mode() == 2) 
        printf("\033[%d;1H\033[2K 👉 명령: reset / clearlog / log / speed <N> / stop / start / exit", 7 + DASHBOARD_LOGS);
    else 
        printf("\033[%d;1H\033[2K 👉 명령: log / exit", 7 + DASHBOARD_LOGS);
    
    printf("\033[u"); 
    fflush(stdout); 
    pthread_mutex_unlock(&screen_mutex); 
}

void browse_logs(int start_page) {
    is_browsing_log = 1;
    usleep(50000); 
    int page = start_page;
    int items_per_page = 15;
    
    while(1) {
        pthread_mutex_lock(&screen_mutex);
        printf("\033[2J\033[1;1H"); 

        pthread_mutex_lock(&log_mutex);
        int total_pages = (total_logs + items_per_page - 1) / items_per_page;
        if (total_pages == 0) total_pages = 1;
        if (page < 1) page = 1;
        if (page > total_pages) page = total_pages;

        printf(" ========================================================================\n");
        printf("        서버 전체 로그 기록 (페이지 %d / %d)\n", page, total_pages);
        printf(" ========================================================================\n");
        
        if (total_logs == 0) {
            printf("  기록된 로그가 없습니다.\n");
        } else {
            int start = (page - 1) * items_per_page;
            int end = start + items_per_page;
            if (end > total_logs) end = total_logs;
            
            for (int i = start; i < end; i++) {
                int idx = (log_head - 1 - i + MAX_HISTORY) % MAX_HISTORY;
                if(idx < 0) idx += MAX_HISTORY;
                printf("  %d. %s\n", total_logs - i, log_history[idx]);
            }
        }
        pthread_mutex_unlock(&log_mutex);

        printf(" ------------------------------------------------------------------------\n");
        printf(" [0: 닫기 / 숫자: 해당 페이지 이동] >> ");
        fflush(stdout);
        pthread_mutex_unlock(&screen_mutex);
        
        char log_cmd[20];
        if(fgets(log_cmd, sizeof(log_cmd), stdin)) {
            log_cmd[strcspn(log_cmd, "\n")] = 0;
            if (strcmp(log_cmd, "0") == 0) break;
            int p = atoi(log_cmd);
            if (p > 0 && p <= total_pages) page = p;
        }
    }
    
    is_browsing_log = 0;
    pthread_mutex_lock(&screen_mutex);
    printf("\033[2J\033[1;1H"); 
    pthread_mutex_unlock(&screen_mutex);
    
    time_t vt = get_virtual_time();
    char time_str[26]; print_time_str(vt, time_str);
    draw_dashboard(time_str);

    pthread_mutex_lock(&screen_mutex);
    printf("\033[%d;1H\033[K >> ", 9 + DASHBOARD_LOGS);
    fflush(stdout);
    pthread_mutex_unlock(&screen_mutex);
}

void* admin_console_thread(void* arg) {
    (void)arg;
    char cmd[100];
    
    pthread_mutex_lock(&screen_mutex);
    printf("\033[%d;1H\033[K >> ", 9 + DASHBOARD_LOGS); 
    fflush(stdout);
    pthread_mutex_unlock(&screen_mutex);

    while(1) {
        if (fgets(cmd, sizeof(cmd), stdin)) {
            cmd[strcspn(cmd, "\n")] = 0; 
            
            pthread_mutex_lock(&screen_mutex);
            printf("\033[%d;1H\033[J >> ", 9 + DASHBOARD_LOGS);
            fflush(stdout);
            pthread_mutex_unlock(&screen_mutex);

            if (strcmp(cmd, "exit") == 0) handle_sigint(0);

            if (strncmp(cmd, "log", 3) == 0) {
                int page = 1;
                sscanf(cmd, "log %d", &page);
                browse_logs(page);
                continue; 
            }

            if (get_server_mode() == 2) { 
                if (strcmp(cmd, "reset") == 0) {
                    clear_inventory_db();     // 1. 창고(DB) 비우기
                    reset_virtual_time();     // 2. 가상 시간 초기화
                    set_speed_factor(1);      // 3. 배속 1배로 초기화
                    clear_persistent_logs();  // 4. [추가됨] 전체 로그 기록 삭제
                    
                    update_log("[초기화] 데이터베이스, 설정 및 로그가 모두 초기화되었습니다.");
                }
                else if (strcmp(cmd, "clearlog") == 0) clear_persistent_logs();
                else if (strcmp(cmd, "stop") == 0) { set_clock_showing(0); update_log("[제어] 시계 멈춤"); }
                else if (strcmp(cmd, "start") == 0) { set_clock_showing(1); update_log("[제어] 시계 재개"); }
                else if (strncmp(cmd, "speed", 5) == 0) {
                    int new_spd;
                    if (sscanf(cmd, "speed %d", &new_spd) == 1 && new_spd > 0) {
                        set_speed_factor(new_spd);
                        char buf[100]; snprintf(buf, sizeof(buf), "[설정] 배속 x%d 적용", new_spd);
                        update_log(buf); save_config(); 
                    } else update_log("[오류] 사용법: speed 360");
                }
            } 
        }
    }
    return NULL;
}
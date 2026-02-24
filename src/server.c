#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <signal.h>
#include <stdint.h>
#include <errno.h>

#define PORT 8080
#define MAX_PAYLOAD 8192

// [설정 상수]
#define MAX_HISTORY 1000      
#define DASHBOARD_LOGS 15     
#define CONFIG_FILE "server_config.txt"

// [전역 변수]
int server_mode = 0;        
int speed_factor = 1;       
int show_clock = 1;         
time_t start_real_time;
time_t start_virtual_time;
char db_filename[50];       
char log_filename[50]; 

// [화면 상태 제어 변수]
char log_history[MAX_HISTORY][1024]; 
int log_head = 0; 
int total_logs = 0;
char last_log[1024] = "서버 대기 중...";
int is_browsing_log = 0; 

// [동기화 뮤텍스]
pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER; 
pthread_mutex_t screen_mutex = PTHREAD_MUTEX_INITIALIZER; 

// [구조체 정의]
typedef struct { 
    uint32_t client_id; 
    uint32_t code; 
    uint32_t length; 
} NetHeader;

typedef struct Product {
    char id[20]; char name[50]; time_t expire_time; int is_expired;
    struct Product* next;
} Product;

typedef struct {
    int sock;
    struct sockaddr_in addr;
} ClientInfo;

Product* head = NULL;

char r_types[10][50] = {"김밥", "샌드위치", "우유", "도시락", "컵라면", "콜라", "생수", "과자", "아이스크림", "커피"};
char r_prefixes[10] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J'};
int r_counts[10] = {0}; 

/* ==========================================
   시간 및 유틸리티
   ========================================== */
time_t get_virtual_time() {
    time_t now; time(&now);
    return start_virtual_time + (time_t)(difftime(now, start_real_time) * speed_factor);
}

void print_time_str(time_t t, char* buf) {
    struct tm tm_info; localtime_r(&t, &tm_info);
    strftime(buf, 26, "%Y-%m-%d %H:%M:%S", &tm_info);
}

/* ==========================================
   로그 및 설정 관리
   ========================================== */
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

void load_persistent_logs() {
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

void clear_persistent_logs() {
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

void save_config() {
    if (server_mode != 2) return; 
    FILE *fp = fopen(CONFIG_FILE, "w");
    if (!fp) return;
    
    time_t now; time(&now);
    time_t current_vt = get_virtual_time();
    fprintf(fp, "%d %ld %ld\n", speed_factor, (long)current_vt, (long)now);
    fclose(fp);
}

void load_config() {
    if (server_mode != 2) return;

    FILE *fp = fopen(CONFIG_FILE, "r");
    if (!fp) { speed_factor = 1; return; }

    int saved_speed;
    long saved_vt, saved_rt;
    if (fscanf(fp, "%d %ld %ld", &saved_speed, &saved_vt, &saved_rt) == 3) {
        speed_factor = saved_speed;
        time_t now; time(&now);
        time_t time_diff = now - (time_t)saved_rt; 
        time_t resumed_vt = (time_t)saved_vt + (time_diff * speed_factor);
        
        start_real_time = now;
        start_virtual_time = resumed_vt;

        char msg[256];
        snprintf(msg, sizeof(msg), "[System] 세션 복구 완료 (%ld초 흐름, 배속: x%d)", time_diff, speed_factor);
        update_log(msg);
    }
    fclose(fp);
}

/* ==========================================
   화면 그리기
   ========================================== */
void draw_dashboard(const char* time_str) {
    if (!show_clock || is_browsing_log) return; 

    pthread_mutex_lock(&screen_mutex); 
    printf("\033[s"); 

    printf("\033[1;1H\033[2K========================================================================");
    if (server_mode == 2)
        printf("\033[2;1H\033[2K [SIMULATION] 배속: x%-5d | DB: %-20s", speed_factor, db_filename);
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

    if (server_mode == 2) 
        printf("\033[%d;1H\033[2K 👉 명령: reset / clearlog / log / speed <N> / stop / start / exit", 7 + DASHBOARD_LOGS);
    else 
        printf("\033[%d;1H\033[2K 👉 명령: log / exit", 7 + DASHBOARD_LOGS);
    
    printf("\033[u"); 
    fflush(stdout); 
    pthread_mutex_unlock(&screen_mutex); 
}

/* ==========================================
   데이터 관리 및 비즈니스 로직
   ========================================== */
int compare_products(const void* a, const void* b) {
    Product* p1 = *(Product**)a; Product* p2 = *(Product**)b;
    return (p1->expire_time < p2->expire_time) ? -1 : (p1->expire_time > p2->expire_time);
}

int is_id_exists(char* id) {
    for(Product* c = head; c; c = c->next) if(strcmp(c->id, id) == 0) return 1;
    return 0;
}

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

void save_data() {
    FILE *fp = fopen(db_filename, "w");
    if (!fp) return;
    for(Product* c = head; c; c = c->next)
        fprintf(fp, "%s %s %ld %d\n", c->id, c->name, (long)c->expire_time, c->is_expired);
    fclose(fp);
    save_config(); 
}

void free_all_resources() {
    Product* cur = head;
    while(cur) { Product* next = cur->next; free(cur); cur = next; }
    head = NULL;
    for(int i=0; i<10; i++) r_counts[i] = 0;
}

void load_data() {
    FILE *fp = fopen(db_filename, "r");
    if (!fp) { update_log("[System] 새로운 데이터베이스 생성"); return; }
    
    char id[20], name[50]; long et; int ie; int cnt = 0;
    while(fscanf(fp, "%s %s %ld %d", id, name, &et, &ie) == 4) {
        Product* n = malloc(sizeof(Product));
        if (n) { 
            strcpy(n->id, id); strcpy(n->name, name);
            n->expire_time = (time_t)et; n->is_expired = ie;
            n->next = head; head = n; cnt++;
            char pre; int num;
            if (sscanf(id, "%c_%d", &pre, &num) == 2) {
                for(int i=0; i<10; i++) if(r_prefixes[i] == pre && num > r_counts[i]) r_counts[i] = num;
            }
        }
    }
    fclose(fp);
    char buf[100]; snprintf(buf, sizeof(buf), "[System] 기존 데이터 %d개 로드됨", cnt);
    update_log(buf);
}

void handle_sigint(int sig) {
    printf("\033[?25h"); 
    printf("\n\n[System] 데이터 저장 및 서버 종료 중...\n");
    pthread_mutex_lock(&list_mutex); save_data(); pthread_mutex_unlock(&list_mutex);
    free_all_resources(); exit(0);
}

void make_category_summary(char* out, int mode, const char* title) {
    typedef struct { char name[50]; int count; } Cat;
    Cat cats[100]; int n = 0;
    for(Product* c = head; c; c = c->next) {
        if((mode==1 && !c->is_expired) || (mode==2 && c->is_expired)) continue;
        int f = -1; for(int i=0; i<n; i++) if(strcmp(cats[i].name, c->name)==0) { f=i; break; }
        if(f>=0) cats[f].count++;
        else if(n<100) { strcpy(cats[n].name, c->name); cats[n++].count = 1; }
    }
    sprintf(out, "\n=== %s ===\n", title);
    if(n==0) strcat(out, "상품이 없습니다.\n");
    else for(int i=0; i<n; i++) {
        char t[100]; sprintf(t, " - %-15.40s : %d개\n", cats[i].name, cats[i].count); strcat(out, t);
    }
}

int make_detail_page(char* out, const char* name, int page, int mode) {
    int total = 0;
    for(Product* c = head; c; c = c->next) 
        if(strcmp(c->name, name)==0 && !(mode==1 && !c->is_expired)) total++;
    int items = 15; int tp = (total + items - 1) / items;
    
    if(tp == 0) tp = 1; 
    if(page < 1) page = 1; 
    if(page > tp) page = tp;

    sprintf(out, "\n=== [%.40s] %s (페이지 %d/%d) ===\n", name, mode==1?"만료 목록":"상세 목록", page, tp);
    if(total > 0) {
        Product** arr = malloc(sizeof(Product*) * total); int idx = 0;
        for(Product* c = head; c; c = c->next)
            if(strcmp(c->name, name)==0 && !(mode==1 && !c->is_expired)) arr[idx++] = c;
        qsort(arr, total, sizeof(Product*), compare_products);
        int start = (page-1)*items; int end = (start+items > total)? total : start+items;
        for(int i=start; i<end; i++) {
            char t[200], ts[26]; print_time_str(arr[i]->expire_time, ts);
            snprintf(t, sizeof(t), "  [%s] %s | %s\n", arr[i]->id, arr[i]->is_expired?"만료":"정상", ts);
            strcat(out, t);
        }
        free(arr);
    } else strcat(out, "상품이 없습니다.\n");
    return tp;
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
        
        pthread_mutex_lock(&list_mutex);
        int out_p = 0;

        switch(cmd) {
            case 99: {
                snprintf(msg, sizeof(msg), "[접속] 단말기 [POS-%04d] 실행됨 (IP: %s)", cid, client_ip);
                update_log(msg);
                break;
            }
            case 100: {
                snprintf(msg, sizeof(msg), "[종료] 단말기 [POS-%04d] 종료됨", cid);
                update_log(msg);
                break;
            }

            case 1: { 
                char id[20], name[50]; int h;
                if(sscanf(pin, "%19[^|]|%49[^|]|%d", id, name, &h) == 3) {
                    if(is_id_exists(id)) snprintf(msg, sizeof(msg), "[오류] 중복 ID: %s", id);
                    else {
                        Product* n = malloc(sizeof(Product));
                        if (!n) snprintf(msg, sizeof(msg), "[오류] 메모리 부족");
                        else {
                            strncpy(n->id, id, 19); n->id[19] = '\0';
                            strncpy(n->name, name, 49); n->name[49] = '\0';
                            n->expire_time = get_virtual_time() + (h*3600); n->is_expired = 0;
                            n->next = head; head = n; save_data();
                            snprintf(msg, sizeof(msg), "[POS-%04d] 단일입고: %s", cid, id);
                            update_log(msg); 
                        }
                    }
                }
                break;
            }
            case 2: { 
                int q = atoi(pin);
                if (q > 0) {
                    Product* local_head = NULL, *local_tail = NULL;
                    int actual_q = 0; 
                    for(int i=0; i<q; i++) {
                        int r = rand()%10; char nid[20];
                        snprintf(nid, sizeof(nid), "%c_%04d", r_prefixes[r], ++r_counts[r]);
                        Product* n = malloc(sizeof(Product));
                        if (!n) { snprintf(msg, sizeof(msg), "[오류] 메모리 부족"); break; }
                        strcpy(n->id, nid); strcpy(n->name, r_types[r]);
                        n->expire_time = get_virtual_time() + ((rand()%96+1)*3600);
                        n->is_expired = 0;
                        n->next = local_head; local_head = n;
                        if (local_tail == NULL) local_tail = n;
                        actual_q++;
                    }
                    if (local_tail != NULL) { local_tail->next = head; head = local_head; }
                    save_data(); 
                    if (msg[0] == '\0') snprintf(msg, sizeof(msg), "[POS-%04d] 랜덤입고 %d개", cid, actual_q);
                    update_log(msg); 
                }
                break;
            }
            case 7: { make_category_summary(msg, 0, "전체 재고 요약"); break; }
            case 10: { make_category_summary(msg, 1, "만료 재고 요약"); break; }
            case 15: { make_category_summary(msg, 2, "판매 가능 메뉴판"); break; }
            
            case 9:  
            case 11: { 
                char *saveptr;
                char *n = strtok_r(pin, "|", &saveptr); 
                char *p = strtok_r(NULL, "|", &saveptr); 
                if(n && p) out_p = make_detail_page(msg, n, atoi(p), (cmd == 11 ? 1 : 0));
                break;
            }
            case 14: { 
                char name[50]; int req_qty, total = 0; 
                sscanf(pin, "%49[^|]|%d", name, &req_qty);
                for(Product* c = head; c; c = c->next) {
                    if(strcmp(c->name, name) == 0 && !c->is_expired) total++;
                }
                if(total == 0) snprintf(msg, sizeof(msg), "[실패] %s 재고 없음", name);
                else {
                    int actual_qty = (total < req_qty) ? total : req_qty;
                    Product** arr = malloc(sizeof(Product*) * total); 
                    if (!arr) snprintf(msg, sizeof(msg), "[오류] 시스템 메모리 부족");
                    else {
                        int idx = 0;
                        for(Product* c = head; c; c = c->next) {
                            if(strcmp(c->name, name) == 0 && !c->is_expired) arr[idx++] = c;
                        }
                        qsort(arr, total, sizeof(Product*), compare_products);
                        for(int i = 0; i < actual_qty; i++) {
                            Product *t = arr[i], *cur = head, *prev = NULL;
                            while(cur) {
                                if(cur == t) { 
                                    if(!prev) head = cur->next; else prev->next = cur->next; 
                                    free(cur); break; 
                                }
                                prev = cur; cur = cur->next;
                            }
                        }
                        free(arr); save_data(); 
                        if (total < req_qty) snprintf(msg, sizeof(msg), "[POS-%04d] 부분판매: %s %d개 (요청:%d)", cid, name, actual_qty, req_qty);
                        else snprintf(msg, sizeof(msg), "[POS-%04d] 판매완료: %s %d개", cid, name, actual_qty);
                    }
                    update_log(msg); 
                }
                break;
            }
            case 16: { 
                free_all_resources(); remove(db_filename);
                snprintf(msg, sizeof(msg), "[POS-%04d] 창고 비움", cid); 
                update_log(msg); 
                break;
            }

            // ==========================================
            // [추가] 장바구니 담기 전 사전 재고 검증 로직
            case 17: { 
                char name[50]; int req_qty, total = 0; 
                sscanf(pin, "%49[^|]|%d", name, &req_qty);
                
                for(Product* c = head; c; c = c->next) {
                    if(strcmp(c->name, name) == 0 && !c->is_expired) total++;
                }
                
                // 1. 재고가 아예 0개(또는 없는 상품)일 때만 엄격하게 차단!
                if (total == 0) {
                    snprintf(msg, sizeof(msg), "[실패] '%s' 상품은 존재하지 않거나 재고가 없습니다.", name);
                } 
                // 2. 재고가 1개라도 있으면 요구 수량에 상관없이 일단 무조건 통과! (부분판매 유도)
                else {
                    strcpy(msg, "OK"); 
                }
                break;
            }
            // ==========================================

            case 5: case 6: case 8: case 12: case 13: {
                 Product *cur = head, *prev = NULL; int d = 0; int f = 0;
                 if (cmd == 5) {
                    while(cur) {
                        if(cur->is_expired) { Product* t=cur; if(!prev) head=cur->next; else prev->next=cur->next; cur=cur->next; free(t); d++; }
                        else { prev=cur; cur=cur->next; }
                    }
                    snprintf(msg, sizeof(msg), "[POS-%04d] 삭제: 만료 일괄 폐기 %d개", cid, d);
                    save_data(); update_log(msg);
                 } 
                 else if (cmd == 8 || cmd == 6) {
                    while(cur) {
                        if(strcmp(cur->id, pin)==0) {
                            if(cmd==6 && !cur->is_expired) strcpy(msg, "[실패] 미만료 상품");
                            else { 
                                if(!prev) head=cur->next; else prev->next=cur->next; free(cur); 
                                snprintf(msg, sizeof(msg), "[POS-%04d] 단일삭제: %.100s", cid, pin); 
                                save_data(); update_log(msg); 
                            }
                            f=1; break;
                        }
                        prev=cur; cur=cur->next;
                    }
                    if(!f) strcpy(msg, "[실패] ID 없음");
                 } 
                 else { 
                    while(cur) {
                        if(strcmp(cur->name, pin)==0 && (cmd==12 || cur->is_expired)) {
                            Product* t=cur; if(!prev) head=cur->next; else prev->next=cur->next;
                            cur=cur->next; free(t); d++;
                        } else { prev=cur; cur=cur->next; }
                    }
                    snprintf(msg, sizeof(msg), "[POS-%04d] 종류삭제: %d개 삭제", cid, d); 
                    save_data(); update_log(msg);
                 }
                 break;
            }
        } 

        pthread_mutex_unlock(&list_mutex);

        snprintf(pout, sizeof(pout), "%d|%.8100s", out_p, msg);
        res.code = htonl(200); res.length = htonl(strlen(pout));
        send_exact(sock, &res, sizeof(NetHeader));
        send_exact(sock, pout, strlen(pout));
    }

    close(sock); 
    return NULL;
}
/* ==========================================
   스레드: 관리자 입력 및 모니터링
   ========================================== */
void* admin_console_thread(void* arg) {
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
                is_browsing_log = 1;
                usleep(50000); 

                int page = 1;
                sscanf(cmd, "log %d", &page);
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
                continue; 
            }

            if (server_mode == 2) { 
                if (strcmp(cmd, "reset") == 0) {
                    pthread_mutex_lock(&list_mutex);
                    free_all_resources(); remove(db_filename);
                    time(&start_real_time); start_virtual_time = start_real_time;
                    speed_factor = 1; 
                    pthread_mutex_unlock(&list_mutex);
                    update_log("[초기화] 데이터 및 세팅 리셋 완료 (1배속)");
                }
                else if (strcmp(cmd, "clearlog") == 0) {
                    clear_persistent_logs();
                }
                else if (strcmp(cmd, "stop") == 0) { show_clock = 0; update_log("[제어] 시계 멈춤"); }
                else if (strcmp(cmd, "start") == 0) { show_clock = 1; update_log("[제어] 시계 재개"); }
                else if (strncmp(cmd, "speed", 5) == 0) {
                    int new_spd;
                    if (sscanf(cmd, "speed %d", &new_spd) == 1 && new_spd > 0) {
                        time_t now; time(&now);
                        start_virtual_time = start_virtual_time + (time_t)(difftime(now, start_real_time) * speed_factor);
                        start_real_time = now;
                        speed_factor = new_spd;
                        char buf[100]; snprintf(buf, sizeof(buf), "[설정] 배속 x%d 적용", speed_factor);
                        update_log(buf);
                        save_config(); 
                    } else {
                        update_log("[오류] 사용법: speed 360");
                    }
                }
            } 
        }
    }
    return NULL;
}

void* monitor_thread(void* arg) {
    while(1) {
        time_t vt = get_virtual_time();
        char time_str[26]; print_time_str(vt, time_str);

        draw_dashboard(time_str);

        pthread_mutex_lock(&list_mutex);
        int ch = 0;
        for(Product* c = head; c; c = c->next) {
            if(!c->is_expired && c->expire_time < vt) { 
                c->is_expired = 1; ch = 1; 
                char buf[256]; snprintf(buf, sizeof(buf), "[만료 발생] %s", c->name);
                update_log(buf); 
            }
        }
        if(ch) save_data(); 
        pthread_mutex_unlock(&list_mutex);
        
        usleep(500000); 
    }
}

/* ==========================================
   메인 함수
   ========================================== */
int main() {
    printf("\033[2J\033[1;1H");
    printf("======================================\n");
    printf("    스마트 재고 관리 서버 (Server)    \n");
    printf("======================================\n");
    printf("1. 운영 모드 (Operation)\n");
    printf("2. 시뮬레이션 모드 (Simulation)\n");
    printf("--------------------------------------\n");
    printf("선택 >> ");
    
    if (scanf("%d", &server_mode) != 1) {
        server_mode = 1;
    }
    getchar(); 

    if (server_mode == 2) {
        strncpy(db_filename, "sim_db.txt", sizeof(db_filename)-1);
        db_filename[sizeof(db_filename)-1] = '\0';
        
        strncpy(log_filename, "sim_server.log", sizeof(log_filename)-1);
        log_filename[sizeof(log_filename)-1] = '\0';
        
        speed_factor = 1; 
    } else {
        server_mode = 1; 
        speed_factor = 1;
        
        strncpy(db_filename, "oper_db.txt", sizeof(db_filename)-1);
        db_filename[sizeof(db_filename)-1] = '\0';
        
        strncpy(log_filename, "oper_server.log", sizeof(log_filename)-1);
        log_filename[sizeof(log_filename)-1] = '\0';
    }

    srand(time(NULL)); 
    pthread_mutex_init(&list_mutex, NULL);
    pthread_mutex_init(&log_mutex, NULL);
    pthread_mutex_init(&screen_mutex, NULL);

    time(&start_real_time); 
    start_virtual_time = start_real_time;
    
    load_persistent_logs(); 
    load_data(); 
    load_config(); 

    pthread_mutex_lock(&list_mutex);
    time_t current_vt = get_virtual_time(); 
    int recovery_count = 0;

    for(Product* c = head; c; c = c->next) {
        if(!c->is_expired && c->expire_time < current_vt) { 
            c->is_expired = 1;
            
            char buf[256];
            char expire_ts[26];
            print_time_str(c->expire_time, expire_ts); 

            snprintf(buf, sizeof(buf), "[재시작 복구] 중단 중 만료 발생: %s (원래 만료: %s)", c->name, expire_ts);
            
            update_log(buf); 
            recovery_count++;
        }
    }

    if(recovery_count > 0) {
        save_data(); 
    }
    pthread_mutex_unlock(&list_mutex);

    printf("\033[2J\033[1;1H"); 
    signal(SIGINT, handle_sigint);

    pthread_t m_tid, a_tid;
    pthread_create(&m_tid, NULL, monitor_thread, NULL);
    pthread_create(&a_tid, NULL, admin_console_thread, NULL);

    int s_sock, c_sock; struct sockaddr_in s_addr, c_addr; socklen_t len = sizeof(c_addr);
    s_sock = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1; setsockopt(s_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    s_addr.sin_family = AF_INET; s_addr.sin_addr.s_addr = INADDR_ANY; s_addr.sin_port = htons(PORT);
    bind(s_sock, (struct sockaddr *)&s_addr, sizeof(s_addr));
    listen(s_sock, 10);

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
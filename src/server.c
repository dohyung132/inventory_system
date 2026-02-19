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

#define PORT 8080
#define SPEED_FACTOR 360  
#define MAX_PAYLOAD 8192

// [TLV 통신 헤더]
typedef struct {
    uint32_t code;   
    uint32_t length; 
} NetHeader;

typedef struct Product {
    char id[20];
    char name[50];
    time_t expire_time;
    int is_expired;         
    struct Product* next;
} Product;

Product* head = NULL;
pthread_mutex_t list_mutex;
time_t start_real_time;
time_t start_virtual_time;

char r_types[10][50] = {"김밥", "샌드위치", "우유", "도시락", "컵라면", "콜라", "생수", "과자", "아이스크림", "커피"};
char r_prefixes[10] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J'};
int r_counts[10] = {0}; 

/* --- 유틸리티 및 네트워크 보장 함수 --- */

ssize_t send_exact(int sock, const void *buf, size_t len) {
    size_t total = 0; const char *p = (const char *)buf;
    while (total < len) {
        ssize_t n = send(sock, p + total, len - total, 0);
        if (n <= 0) return -1; total += n;
    }
    return total;
}

ssize_t recv_exact(int sock, void *buf, size_t len) {
    size_t total = 0; char *p = (char *)buf;
    while (total < len) {
        ssize_t n = recv(sock, p + total, len - total, 0);
        if (n <= 0) return -1; total += n;
    }
    return total;
}

time_t get_virtual_time() {
    time_t now; time(&now);
    return start_virtual_time + (time_t)(difftime(now, start_real_time) * SPEED_FACTOR);
}

void print_time_str(time_t t, char* buf) {
    struct tm tm_info; localtime_r(&t, &tm_info);
    strftime(buf, 26, "%Y-%m-%d %H:%M:%S", &tm_info);
}

int compare_products(const void* a, const void* b) {
    Product* p1 = *(Product**)a; Product* p2 = *(Product**)b;
    return (p1->expire_time < p2->expire_time) ? -1 : (p1->expire_time > p2->expire_time);
}

int is_id_exists(char* id) {
    for(Product* c = head; c; c = c->next) if(strcmp(c->id, id) == 0) return 1;
    return 0;
}

/* --- 자원 관리 및 영속성 --- */

void save_data() {
    FILE *fp = fopen("inventory_data.txt", "w");
    if (!fp) return;
    for(Product* c = head; c; c = c->next)
        fprintf(fp, "%s %s %ld %d\n", c->id, c->name, (long)c->expire_time, c->is_expired);
    fclose(fp);
}

void free_all_resources() {
    pthread_mutex_lock(&list_mutex);
    Product* cur = head;
    while(cur) { Product* next = cur->next; free(cur); cur = next; }
    head = NULL;
    pthread_mutex_unlock(&list_mutex);
}

void load_data() {
    FILE *fp = fopen("inventory_data.txt", "r");
    if (!fp) return;
    char id[20], name[50]; long et; int ie; int cnt = 0;
    while(fscanf(fp, "%s %s %ld %d", id, name, &et, &ie) == 4) {
        Product* n = malloc(sizeof(Product));
        strcpy(n->id, id); strcpy(n->name, name);
        n->expire_time = (time_t)et; n->is_expired = ie;
        n->next = head; head = n; cnt++;
        char pre; int num;
        if (sscanf(id, "%c_%d", &pre, &num) == 2) {
            for(int i=0; i<10; i++) if(r_prefixes[i] == pre && num > r_counts[i]) r_counts[i] = num;
        }
    }
    fclose(fp);
    printf("[System] 데이터 %d개 로드 완료\n", cnt);
}

void handle_sigint(int sig) {
    printf("\n[System] 종료 중... 자원 정리 실시\n");
    pthread_mutex_lock(&list_mutex); save_data(); pthread_mutex_unlock(&list_mutex);
    free_all_resources(); exit(0);
}

/* --- 공통 비즈니스 로직 --- */

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
    if(tp==0) tp=1; if(page<1) page=1; if(page>tp) page=tp;
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

/* --- 클라이언트 핸들러 --- */

void* client_handler(void* socket_desc) {
    int sock = *(int*)socket_desc; free(socket_desc);
    NetHeader req, res;
    char pin[MAX_PAYLOAD], pout[MAX_PAYLOAD], msg[MAX_PAYLOAD];

    while (recv_exact(sock, &req, sizeof(NetHeader)) > 0) {
        uint32_t cmd = ntohl(req.code), len = ntohl(req.length);
        if(len > 0) recv_exact(sock, pin, (len < MAX_PAYLOAD)? len : MAX_PAYLOAD-1);
        pin[len] = '\0'; msg[0] = '\0';
        pthread_mutex_lock(&list_mutex);
        int out_p = 0;

        if(cmd == 1) { // 수동 입고
            char id[20], name[50]; int h;
            if(sscanf(pin, "%19[^|]|%49[^|]|%d", id, name, &h) == 3) {
                if(is_id_exists(id)) snprintf(msg, sizeof(msg), "❌ 중복 ID: %.20s", id);
                else {
                    Product* n = malloc(sizeof(Product));
                    strncpy(n->id, id, 19); n->id[19] = '\0';
                    strncpy(n->name, name, 49); n->name[49] = '\0';
                    n->expire_time = get_virtual_time() + (h*3600); n->is_expired = 0;
                    n->next = head; head = n; save_data();
                    snprintf(msg, sizeof(msg), "✅ 입고 완료: %.20s", id);
                }
            }
        }
        else if(cmd == 2) { // 대량 랜덤 입고 (개선 버전)
            int q = atoi(pin);
            for(int i=0; i<q; i++) {
                int r = rand()%10; char nid[20];
                snprintf(nid, sizeof(nid), "%c_%04d", r_prefixes[r], ++r_counts[r]);
                Product* n = malloc(sizeof(Product));
                if(n) {
                    strcpy(n->id, nid); strcpy(n->name, r_types[r]);
                    n->expire_time = get_virtual_time() + ((rand()%96+1)*3600);
                    n->is_expired = 0; n->next = head; head = n;
                }
            }
            save_data(); snprintf(msg, sizeof(msg), "✅ 랜덤 %d개 입고 완료", q);
        }
        else if(cmd == 7) make_category_summary(msg, 0, "전체 재고 요약");
        else if(cmd == 10) make_category_summary(msg, 1, "만료 재고 요약");
        else if(cmd == 15) make_category_summary(msg, 2, "판매 가능 메뉴판");
        else if(cmd == 9) { char* n=strtok(pin,"|"); char* p=strtok(NULL,"|"); if(n&&p) out_p=make_detail_page(msg, n, atoi(p), 0); }
        else if(cmd == 11) { char* n=strtok(pin,"|"); char* p=strtok(NULL,"|"); if(n&&p) out_p=make_detail_page(msg, n, atoi(p), 1); }
        else if(cmd == 14) { // 판매 (FIFO)
            char name[50]; int qty, total=0; sscanf(pin, "%49[^|]|%d", name, &qty);
            for(Product* c=head; c; c=c->next) if(strcmp(c->name, name)==0 && !c->is_expired) total++;
            if(total < qty) snprintf(msg, sizeof(msg), "❌ 재고 부족 (현재: %d)", total);
            else {
                Product** arr = malloc(sizeof(Product*)*total); int idx=0;
                for(Product* c=head; c; c=c->next) if(strcmp(c->name, name)==0 && !c->is_expired) arr[idx++]=c;
                qsort(arr, total, sizeof(Product*), compare_products);
                for(int i=0; i<qty; i++) {
                    Product *t=arr[i], *cur=head, *prev=NULL;
                    while(cur) {
                        if(cur==t) { if(!prev) head=cur->next; else prev->next=cur->next; free(cur); break; }
                        prev=cur; cur=cur->next;
                    }
                }
                free(arr); save_data(); snprintf(msg, sizeof(msg), "✅ %.40s %d개 판매 완료", name, qty);
            }
        }
        else if(cmd == 16) { 
            pthread_mutex_unlock(&list_mutex); free_all_resources(); pthread_mutex_lock(&list_mutex);
            for(int i=0; i<10; i++) r_counts[i]=0;
            strcpy(msg, "🔥 창고 전체 초기화 완료"); save_data();
        }
        else if(cmd == 12 || cmd == 13) { 
            Product *cur=head, *prev=NULL; int d=0;
            while(cur) {
                if(strcmp(cur->name, pin)==0 && (cmd==12 || cur->is_expired)) {
                    Product* t=cur; if(!prev) head=cur->next; else prev->next=cur->next;
                    cur=cur->next; free(t); d++;
                } else { prev=cur; cur=cur->next; }
            }
            snprintf(msg, sizeof(msg), "✅ %.40s %d개 삭제", pin, d); save_data();
        }
        else if(cmd == 8 || cmd == 6) { 
            Product *cur=head, *prev=NULL; int f=0;
            while(cur) {
                if(strcmp(cur->id, pin)==0) {
                    if(cmd==6 && !cur->is_expired) strcpy(msg, "❌ 미만료 상품");
                    else { if(!prev) head=cur->next; else prev->next=cur->next; free(cur); strcpy(msg, "✅ 삭제 완료"); save_data(); }
                    f=1; break;
                }
                prev=cur; cur=cur->next;
            }
            if(!f) strcpy(msg, "❌ ID 없음");
        }
        else if(cmd == 5) { 
            Product *cur=head, *prev=NULL; int d=0;
            while(cur) {
                if(cur->is_expired) { Product* t=cur; if(!prev) head=cur->next; else prev->next=cur->next; cur=cur->next; free(t); d++; }
                else { prev=cur; cur=cur->next; }
            }
            snprintf(msg, sizeof(msg), "✅ 만료 상품 %d개 삭제", d); save_data();
        }

        pthread_mutex_unlock(&list_mutex);
        snprintf(pout, sizeof(pout), "%d|%.8000s", out_p, msg);
        res.code = htonl(200); res.length = htonl(strlen(pout));
        send_exact(sock, &res, sizeof(NetHeader));
        send_exact(sock, pout, strlen(pout));
    }
    close(sock); return NULL;
}

void* monitor_thread(void* arg) {
    while(1) {
        time_t vt = get_virtual_time();
        pthread_mutex_lock(&list_mutex);
        int ch = 0;
        for(Product* c = head; c; c = c->next) if(!c->is_expired && c->expire_time < vt) { c->is_expired = 1; ch = 1; }
        if(ch) save_data();
        pthread_mutex_unlock(&list_mutex);
        usleep(500000);
    }
}

int main() {
    int s_sock, c_sock; struct sockaddr_in s_addr, c_addr; socklen_t len = sizeof(c_addr);
    srand(time(NULL)); pthread_mutex_init(&list_mutex, NULL);
    time(&start_real_time); start_virtual_time = start_real_time;
    load_data(); signal(SIGINT, handle_sigint);
    pthread_t m_tid; pthread_create(&m_tid, NULL, monitor_thread, NULL);

    s_sock = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1; setsockopt(s_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    s_addr.sin_family = AF_INET; s_addr.sin_addr.s_addr = INADDR_ANY; s_addr.sin_port = htons(PORT);
    bind(s_sock, (struct sockaddr *)&s_addr, sizeof(s_addr));
    listen(s_sock, 10);
    printf("=== 스마트 재고 서버 가동 (Port: %d) ===\n", PORT);

    while((c_sock = accept(s_sock, (struct sockaddr *)&c_addr, &len))) {
        int* n_sock = malloc(sizeof(int));
        if(n_sock) { *n_sock = c_sock; pthread_t c_tid; pthread_create(&c_tid, NULL, client_handler, n_sock); pthread_detach(c_tid); }
    }
    return 0;
}
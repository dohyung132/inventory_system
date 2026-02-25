#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "inventory.h"
#include "utils.h"
#include "logger.h"

// [내부 데이터 구조체 은닉]
typedef struct Product {
    char id[20]; 
    char name[50]; 
    time_t expire_time; 
    int is_expired;
    struct Product* next;
} Product;

static Product* head = NULL;
pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER;

extern char db_filename[50];

static char r_types[10][50] = {"김밥", "샌드위치", "우유", "도시락", "컵라면", "콜라", "생수", "과자", "아이스크림", "커피"};
static char r_prefixes[10] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J'};
static int r_counts[10] = {0};

// [내부 헬퍼 함수]
static int compare_products(const void* a, const void* b) {
    Product* p1 = *(Product**)a; Product* p2 = *(Product**)b;
    return (p1->expire_time < p2->expire_time) ? -1 : (p1->expire_time > p2->expire_time);
}

static int is_id_exists(char* id) {
    for(Product* c = head; c; c = c->next) if(strcmp(c->id, id) == 0) return 1;
    return 0;
}

// [공개 API 구현]
void init_inventory(void) {
    head = NULL;
    for(int i=0; i<10; i++) r_counts[i] = 0;
}

void save_data(void) {
    FILE *fp = fopen(db_filename, "w");
    if (!fp) return;
    for(Product* c = head; c; c = c->next)
        fprintf(fp, "%s %s %ld %d\n", c->id, c->name, (long)c->expire_time, c->is_expired);
    fclose(fp);
    save_config(); 
}

void load_data(void) {
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

void free_all_resources(void) {
    Product* cur = head;
    while(cur) { Product* next = cur->next; free(cur); cur = next; }
    init_inventory();
}

void clear_inventory_db(void) {
    free_all_resources();
    remove(db_filename);
}

void handle_single_import(uint32_t cid, char* pin, char* msg) {
    pthread_mutex_lock(&list_mutex);
    char id[20], name[50]; int h;
    
    if(sscanf(pin, "%19[^|]|%49[^|]|%d", id, name, &h) == 3) {
        if(is_id_exists(id)) {
            snprintf(msg, MAX_PAYLOAD, "[오류] 중복 ID: %s", id);
        } else {
            // ==========================================
            // [추가된 로직] ID 접두사와 상품명 매칭 검증
            // ==========================================
            int valid_prefix = 0;
            for (int i = 0; i < 10; i++) {
                if (id[0] == r_prefixes[i]) { // 앞글자 매칭 (A, B, C...)
                    if (strcmp(name, r_types[i]) == 0) {
                        valid_prefix = 1; // 완벽히 일치
                    } else {
                        snprintf(msg, MAX_PAYLOAD, "[오류] ID 접두사('%c')와 상품명('%s')이 불일치합니다. (올바른 상품: %s)", id[0], name, r_types[i]);
                        valid_prefix = -1; // 접두사는 맞는데 이름이 틀림
                    }
                    break;
                }
            }

            if (valid_prefix == 0) {
                snprintf(msg, MAX_PAYLOAD, "[오류] 알 수 없는 ID 접두사입니다. (A~J 문자 사용)");
            } 
            else if (valid_prefix == 1) {
                // 검증 통과 시에만 정상 등록
                Product* n = malloc(sizeof(Product));
                if (!n) {
                    snprintf(msg, MAX_PAYLOAD, "[오류] 메모리 부족");
                } else {
                    strncpy(n->id, id, 19); n->id[19] = '\0';
                    strncpy(n->name, name, 49); n->name[49] = '\0';
                    n->expire_time = get_virtual_time() + (h*3600); 
                    n->is_expired = 0;
                    
                    n->next = head; head = n; 
                    save_data(); // DB 저장
                    
                    snprintf(msg, MAX_PAYLOAD, "[POS-%04d] 단일입고: %s (%s)", cid, id, name);
                    update_log(msg); 
                }
            }
        }
    }
    pthread_mutex_unlock(&list_mutex);
}

void handle_random_import(uint32_t cid, char* pin, char* msg) {
    pthread_mutex_lock(&list_mutex);
    int q = atoi(pin);
    if (q > 0) {
        Product* local_head = NULL, *local_tail = NULL;
        int actual_q = 0; 
        for(int i=0; i<q; i++) {
            int r = rand()%10; char nid[20];
            snprintf(nid, sizeof(nid), "%c_%04d", r_prefixes[r], ++r_counts[r]);
            Product* n = malloc(sizeof(Product));
            if (!n) { snprintf(msg, MAX_PAYLOAD, "[오류] 메모리 부족"); break; }
            strcpy(n->id, nid); strcpy(n->name, r_types[r]);
            n->expire_time = get_virtual_time() + ((rand()%96+1)*3600);
            n->is_expired = 0;
            n->next = local_head; local_head = n;
            if (local_tail == NULL) local_tail = n;
            actual_q++;
        }
        if (local_tail != NULL) { local_tail->next = head; head = local_head; }
        save_data(); 
        if (msg[0] == '\0') snprintf(msg, MAX_PAYLOAD, "[POS-%04d] 랜덤입고 %d개", cid, actual_q);
        update_log(msg); 
    }
    pthread_mutex_unlock(&list_mutex);
}

void handle_sell(uint32_t cid, char* pin, char* msg) {
    pthread_mutex_lock(&list_mutex);
    char name[50]; int req_qty, total = 0; 
    sscanf(pin, "%49[^|]|%d", name, &req_qty);
    for(Product* c = head; c; c = c->next) {
        if(strcmp(c->name, name) == 0 && !c->is_expired) total++;
    }
    if(total == 0) snprintf(msg, MAX_PAYLOAD, "[실패] %s 재고 없음", name);
    else {
        int actual_qty = (total < req_qty) ? total : req_qty;
        Product** arr = malloc(sizeof(Product*) * total); 
        if (!arr) snprintf(msg, MAX_PAYLOAD, "[오류] 시스템 메모리 부족");
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
            if (total < req_qty) snprintf(msg, MAX_PAYLOAD, "[POS-%04d] 부분판매: %s %d개 (요청:%d)", cid, name, actual_qty, req_qty);
            else snprintf(msg, MAX_PAYLOAD, "[POS-%04d] 판매완료: %s %d개", cid, name, actual_qty);
        }
        update_log(msg); 
    }
    pthread_mutex_unlock(&list_mutex);
}

void handle_cart_verify(char* pin, char* msg) {
    pthread_mutex_lock(&list_mutex);
    char name[50]; int req_qty, total = 0; 
    sscanf(pin, "%49[^|]|%d", name, &req_qty);
    for(Product* c = head; c; c = c->next) {
        if(strcmp(c->name, name) == 0 && !c->is_expired) total++;
    }
    if (total == 0) snprintf(msg, MAX_PAYLOAD, "[실패] '%s' 상품은 존재하지 않거나 재고가 없습니다.", name);
    else strcpy(msg, "OK"); 
    pthread_mutex_unlock(&list_mutex);
}

void handle_delete_operations(uint32_t cmd, uint32_t cid, char* pin, char* msg) {
    pthread_mutex_lock(&list_mutex);
    Product *cur = head, *prev = NULL; int d = 0; int f = 0;
    
    if (cmd == 5) {
        while(cur) {
            if(cur->is_expired) { 
                Product* t=cur; 
                if(!prev) head=cur->next; else prev->next=cur->next; 
                cur=cur->next; free(t); d++; 
            }
            else { prev=cur; cur=cur->next; }
        }
        snprintf(msg, MAX_PAYLOAD, "[POS-%04d] 삭제: 만료 일괄 폐기 %d개", cid, d);
        save_data(); update_log(msg);
    } 
    else if (cmd == 8 || cmd == 6) {
        char deleted_name[50] = ""; // 삭제될 상품명을 임시 저장할 버퍼
        
        while(cur) {
            if(strcmp(cur->id, pin)==0) {
                if(cmd==6 && !cur->is_expired) {
                    strcpy(msg, "[실패] 미만료 상품");
                } else { 
                    // 메모리 해제 전 상품명 백업
                    strcpy(deleted_name, cur->name); 
                    
                    if(!prev) head=cur->next; else prev->next=cur->next; free(cur); 
                    
                    // "김밥 [A_0001] 삭제" 형태로 포맷팅
                    snprintf(msg, MAX_PAYLOAD, "[POS-%04d] 단일삭제: %s [%s] 삭제", cid, deleted_name, pin); 
                    save_data(); update_log(msg); 
                }
                f=1; break;
            }
            prev=cur; cur=cur->next;
        }
        if(!f) strcpy(msg, "[실패] ID 없음");
    } 
    else { // cmd 12 (종류 전체 삭제) 또는 13 (종류 중 만료 삭제)
        while(cur) {
            // pin 변수에는 클라이언트가 보낸 "아이스크림" 같은 상품명이 들어있음
            if(strcmp(cur->name, pin)==0 && (cmd==12 || cur->is_expired)) {
                Product* t=cur; if(!prev) head=cur->next; else prev->next=cur->next;
                cur=cur->next; free(t); d++;
            } else { prev=cur; cur=cur->next; }
        }
        
        //  만료 삭제와 일반 종류 삭제를 구분하여 상세 출력
        if (cmd == 13) {
            snprintf(msg, MAX_PAYLOAD, "[POS-%04d] 만료삭제: %s %d개 삭제", cid, pin, d); 
        } else {
            snprintf(msg, MAX_PAYLOAD, "[POS-%04d] 종류삭제: %s %d개 삭제", cid, pin, d); 
        }
        save_data(); update_log(msg);
    }
    pthread_mutex_unlock(&list_mutex);
}

void make_category_summary(char* out, int mode, const char* title) {
    pthread_mutex_lock(&list_mutex);
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
    pthread_mutex_unlock(&list_mutex);
}

int make_detail_page(char* out, const char* name, int page, int mode) {
    pthread_mutex_lock(&list_mutex);
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
    pthread_mutex_unlock(&list_mutex);
    return tp;
}

int check_and_update_expirations(time_t current_vt) {
    pthread_mutex_lock(&list_mutex);
    int ch = 0;
    for(Product* c = head; c; c = c->next) {
        if(!c->is_expired && c->expire_time < current_vt) { 
            c->is_expired = 1; ch = 1; 
            char buf[256]; snprintf(buf, sizeof(buf), "[만료 발생] %s", c->name);
            update_log(buf); 
        }
    }
    if(ch) save_data(); 
    pthread_mutex_unlock(&list_mutex);
    return ch;
}

// src/inventory.c 맨 아래 추가

void recover_missed_expirations(time_t current_vt) {
    pthread_mutex_lock(&list_mutex);
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
}
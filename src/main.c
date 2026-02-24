#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/select.h> 

#define DEFAULT_PORT 8080
#define DEFAULT_IP "127.0.0.1"
#define MAX_PAYLOAD 8192

typedef struct {
    uint32_t client_id; 
    uint32_t code;   
    uint32_t length; 
} NetHeader;

typedef struct {
    char name[50];
    int qty;
} CartItem;

char g_server_ip[50] = DEFAULT_IP;
int g_server_port = DEFAULT_PORT;
uint32_t g_client_id = 0; 

static inline void clear_screen(void) {
    printf("\033[2J\033[H");
}

static void flush_stdin(void) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

int get_safe_input(char *buffer, size_t size) {
    fd_set readfds;
    struct timeval tv;
    
    fflush(stdout); 
    
    while(1) {
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        
        tv.tv_sec = 1; 
        tv.tv_usec = 0;

        int ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
        
        if (ret < 0) continue; 
        if (ret > 0) {
            if (fgets(buffer, size, stdin) != NULL) {
                char *newline = strchr(buffer, '\n');
                if (newline) *newline = '\0';
                else flush_stdin();
                return 0; 
            }
        } else if (ret == 0) {
            int ping_sock = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in serv_addr;
            memset(&serv_addr, 0, sizeof(serv_addr));
            serv_addr.sin_family = AF_INET;
            serv_addr.sin_port = htons(g_server_port);
            inet_pton(AF_INET, g_server_ip, &serv_addr.sin_addr);
            
            if (connect(ping_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
                printf("\r\033[K\033[2J\033[1;1H"); 
                printf("=================================\n");
                printf(" 🚨 [긴급] 서버 연결 끊김 감지! 🚨\n");
                printf("=================================\n");
                
                int retry = 0;
                while(1) {
                    retry++;
                    printf("\r\033[K [시스템] 서버 복구 대기 중... (%d회 시도)", retry);
                    fflush(stdout);
                    close(ping_sock);
                    sleep(2); 
                    
                    ping_sock = socket(AF_INET, SOCK_STREAM, 0);
                    if (connect(ping_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == 0) {
                        close(ping_sock);
                        return 1; 
                    }
                }
            } else {
                close(ping_sock); 
            }
        }
    }
    return -1;
}

int parse_int(const char* str, int* out_val) {
    char *endptr; errno = 0;
    long val = strtol(str, &endptr, 10);
    if (endptr == str || *endptr != '\0' || errno == ERANGE) return 0; 
    *out_val = (int)val;
    return 1; 
}

void pause_screen() {
    printf("\n[엔터 키를 누르면 진행합니다...]");
    char temp[2]; 
    if (get_safe_input(temp, sizeof(temp)) == 1) return; 
}

ssize_t send_exact(int sock, const void *buf, size_t len) {
    size_t total_sent = 0; const char *p = (const char *)buf;
    while (total_sent < len) {
        ssize_t n = send(sock, p + total_sent, len - total_sent, 0);
        if (n <= 0) return -1; 
        total_sent += n;
    }
    return total_sent;
}

ssize_t recv_exact(int sock, void *buf, size_t len) {
    size_t total_recv = 0; char *p = (char *)buf;
    while (total_recv < len) {
        ssize_t n = recv(sock, p + total_recv, len - total_recv, 0);
        if (n <= 0) return -1;
        total_recv += n;
    }
    return total_recv;
}

int request_server(uint32_t cmd, const char* payload_out, char* msg_in, int* total_pages) {
    int sock; struct sockaddr_in serv_addr;
    NetHeader req_hdr, res_hdr; char payload_in[MAX_PAYLOAD]; int retry_count = 0;

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET; serv_addr.sin_port = htons(g_server_port);
    inet_pton(AF_INET, g_server_ip, &serv_addr.sin_addr);

    while (1) {
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return -1;
        if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == 0) {
            break; 
        }
        retry_count++;
        printf("\r\033[K[통신] 서버 처리 지연... (%d회)", retry_count); fflush(stdout);
        close(sock); sleep(2);
    }

    uint32_t out_len = payload_out ? strlen(payload_out) : 0;
    
    req_hdr.client_id = htonl(g_client_id);
    req_hdr.code = htonl(cmd); 
    req_hdr.length = htonl(out_len);
    
    if (send_exact(sock, &req_hdr, sizeof(NetHeader)) < 0) { close(sock); return -1; }
    if (out_len > 0 && send_exact(sock, payload_out, out_len) < 0) { close(sock); return -1; }
    if (recv_exact(sock, &res_hdr, sizeof(NetHeader)) < 0) { close(sock); return -1; }
    
    uint32_t res_code = ntohl(res_hdr.code);
    uint32_t in_len = ntohl(res_hdr.length);
    if (in_len > MAX_PAYLOAD - 1) { close(sock); return -1; }
    
    if (in_len > 0) { if (recv_exact(sock, payload_in, in_len) < 0) { close(sock); return -1; } }
    payload_in[in_len] = '\0'; 
    close(sock);

    if (res_code == 200) {
        char* sep = strchr(payload_in, '|');
        if (sep) {
            *sep = '\0';
            if (total_pages) *total_pages = atoi(payload_in);
            strcpy(msg_in, sep + 1);
        } else {
            if (total_pages) *total_pages = 0;
            strcpy(msg_in, payload_in);
        }
    }
    return 0; 
}

int is_numeric(const char *str) {
    if (str == NULL || *str == '\0') return 0;
    for (int i = 0; str[i] != '\0'; i++) if (str[i] < '0' || str[i] > '9') return 0; 
    return 1;
}

void handle_category_view(int sum_cmd, int det_cmd, int del_cat_cmd, int del_item_cmd, int del_all_cmd, const char* title) {
    char payload[MAX_PAYLOAD], response[MAX_PAYLOAD]; int pages = 0;

    while(1) {
        clear_screen();
        if (request_server(sum_cmd, "", response, &pages) < 0) return;
        printf("%s\n", response); 
        if (strstr(response, "없습니다") != NULL) { pause_screen(); break; }

        printf("\n[%s] 상세조회할 '상품명'을 정확히 입력하세요.\n", title);
        if (del_all_cmd != 0) printf("('all': 등록된 전체 일괄 삭제, 뒤로가기: '0') => ");
        else printf("(뒤로가기: '0') => ");
        
        char cat_name[50]; 
        if (get_safe_input(cat_name, sizeof(cat_name)) == 1) continue; 

        if (strcmp(cat_name, "0") == 0) break; 
        else if (del_all_cmd != 0 && strcmp(cat_name, "all") == 0) {
            if (request_server(del_all_cmd, "", response, &pages) < 0) return;
            printf("\n[서버 응답] %s\n", response); pause_screen(); continue; 
        }

        int current_page = 1; 
        while(1) {
            clear_screen();
            snprintf(payload, sizeof(payload), "%s|%d", cat_name, current_page);
            if (request_server(det_cmd, payload, response, &pages) < 0) return;
            
            printf("%s\n", response); 
            if(strstr(response, "해당") != NULL && strstr(response, "없습니다") != NULL) { pause_screen(); break; }

            printf("\n---------------------------------\n");
            printf("  - 상품 ID : 해당 상품 지정 삭제\n  - 'all'   : [%s] 카테고리 전체 삭제\n  - 숫자    : 페이지 이동\n  - '0'     : 뒤로가기\n", cat_name);
            printf("---------------------------------\n입력: ");
            
            char input[50]; 
            if (get_safe_input(input, sizeof(input)) == 1) continue; 

            if (strcmp(input, "0") == 0) break; 
            else if (strcmp(input, "all") == 0) {
                if (request_server(del_cat_cmd, cat_name, response, &pages) < 0) return;
                printf("\n[서버 응답] %s\n", response); pause_screen(); break; 
            }
            else if (is_numeric(input)) {
                int page_num;
                if (parse_int(input, &page_num) && page_num >= 1 && page_num <= pages) current_page = page_num;
                else { printf("\n[오류] 존재하는 페이지 번호를 입력하세요. (1 ~ %d)\n", pages); pause_screen(); }
            }
            else { 
                if (request_server(del_item_cmd, input, response, &pages) < 0) return;
                printf("\n[서버 응답] %s\n", response); pause_screen(); 
            }
        } 
    }
}

int main(int argc, char *argv[]) {
    g_client_id = getpid() % 10000;

    if (argc > 1) strncpy(g_server_ip, argv[1], sizeof(g_server_ip)-1);
    if (argc > 2) g_server_port = atoi(argv[2]);

    char input_buf[100], req_payload[MAX_PAYLOAD], res_payload[MAX_PAYLOAD]; int pages = 0;

    request_server(99, "", res_payload, &pages);

    while(1) {
        clear_screen();
        printf("\n=================================\n");
        printf("  스마트 재고 관리 [POS-%04d]\n", g_client_id);
        printf("=================================\n");
        printf("1. 수동 단일 입고\n2. 랜덤 대량 입고 (10종)\n3. 전체 재고 조회/삭제\n4. 만료 재고 조회/삭제\n5. 상품 판매 (장바구니)\n0. 프로그램 종료\n");
        printf("---------------------------------\n선택: ");
        
        if (get_safe_input(input_buf, sizeof(input_buf)) == 1) continue;
        
        int choice; if (!parse_int(input_buf, &choice)) continue;

        if (choice == 1) {
            char id[20], name[50]; int valid_hours;
            printf("상품 ID (예: Z_01): "); if (get_safe_input(id, sizeof(id)) == 1) continue; 
            printf("상품명: "); if (get_safe_input(name, sizeof(name)) == 1) continue;
            printf("유효기간(시간): "); if (get_safe_input(input_buf, sizeof(input_buf)) == 1) continue;
            
            if (!parse_int(input_buf, &valid_hours) || valid_hours <= 0) {
                printf("[오류] 유효기간은 1시간 이상이어야 합니다.\n"); pause_screen(); continue;
            }
            snprintf(req_payload, sizeof(req_payload), "%s|%s|%d", id, name, valid_hours);
            if (request_server(1, req_payload, res_payload, &pages) < 0) break;
            printf("\n[서버 응답] %s\n", res_payload); pause_screen(); 
        } 
        else if (choice == 2) {
            printf("몇 개의 랜덤 상품을 입고하시겠습니까?: ");
            if (get_safe_input(input_buf, sizeof(input_buf)) == 1) continue;
            int qty;
            if (!parse_int(input_buf, &qty) || qty <= 0 || qty > 50000) {
                printf("[오류] 1개에서 50000개 사이로 입력해주세요.\n"); pause_screen(); continue;
            }
            snprintf(req_payload, sizeof(req_payload), "%d", qty);
            if (request_server(2, req_payload, res_payload, &pages) < 0) break;
            printf("\n[서버 응답] %s\n", res_payload); pause_screen(); 
        }
        else if (choice == 5) {
            CartItem cart[20]; int cart_size = 0;
            while(1) {
                clear_screen();
                if (request_server(15, "", res_payload, &pages) < 0) break;
                printf("%s\n\n=== 🛒 장바구니 현황 ===\n", res_payload);
                if (cart_size == 0) printf(" [장바구니가 비어있습니다]\n");
                else for(int i=0; i<cart_size; i++) printf(" %d. %s - %d개\n", i+1, cart[i].name, cart[i].qty);
                
                printf("---------------------------------\n담을 '상품명' 입력 (결제: 'pay', 취소: '0'): ");
                char input[50]; 
                if (get_safe_input(input, sizeof(input)) == 1) continue; 

                if (strcmp(input, "0") == 0) { printf("\n장바구니를 비우고 종료합니다.\n"); pause_screen(); break; }
                else if (strcmp(input, "pay") == 0) {
                    if (cart_size == 0) { printf("\n장바구니가 비어있습니다!\n"); pause_screen(); continue; }
                    clear_screen(); printf("\n=== 🧾 영수증 (결제 결과) ===\n\n");
                    for(int i=0; i<cart_size; i++) {
                        snprintf(req_payload, sizeof(req_payload), "%s|%d", cart[i].name, cart[i].qty);
                        if (request_server(14, req_payload, res_payload, &pages) < 0) break;
                        printf("%s\n", res_payload); 
                    }
                    printf("\n=================================\n"); pause_screen(); break;
                }
                else {
                    // ==========================================
                    // [수정] 사전 재고 검증 로직 적용
                    printf("[%s] 몇 개를 담으시겠습니까?: ", input);
                    if (get_safe_input(input_buf, sizeof(input_buf)) == 1) continue;
                    int q;
                    if (parse_int(input_buf, &q) && q > 0) {
                        
                        // 1. 이미 장바구니에 담긴 수량 합산
                        int current_in_cart = 0;
                        for(int i=0; i<cart_size; i++) {
                            if (strcmp(cart[i].name, input) == 0) { current_in_cart = cart[i].qty; break; }
                        }
                        int total_req = current_in_cart + q;
                        
                        // 2. 서버에 사전 재고 확인 (cmd 17)
                        snprintf(req_payload, sizeof(req_payload), "%s|%d", input, total_req);
                        if (request_server(17, req_payload, res_payload, &pages) < 0) break;
                        
                        // 3. 응답에 따른 처리
                        if (strcmp(res_payload, "OK") == 0) {
                            int found = 0;
                            for(int i=0; i<cart_size; i++) if (strcmp(cart[i].name, input) == 0) { cart[i].qty += q; found = 1; break; }
                            if (!found) {
                                if (cart_size < 20) { strncpy(cart[cart_size].name, input, 49); cart[cart_size].qty = q; cart_size++; } 
                                else { printf("\n[오류] 최대 20종까지만 담을 수 있습니다.\n"); pause_screen(); }
                            }
                        } else {
                            printf("\n%s\n", res_payload); pause_screen();
                        }
                    } else { printf("\n올바른 수량을 입력해주세요.\n"); pause_screen(); }
                    // ==========================================
                }
            }
        }
        else if (choice == 3) handle_category_view(7, 9, 12, 8, 16, "전체 재고"); 
        else if (choice == 4) handle_category_view(10, 11, 13, 6, 5, "만료 재고"); 
        else if (choice == 0) { 
            request_server(100, "", res_payload, &pages);
            printf("프로그램을 안전하게 종료합니다.\n"); 
            break; 
        }
        else { printf("잘못된 번호입니다.\n"); pause_screen(); }
    }
    return 0;
}
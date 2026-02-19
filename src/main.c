#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>

#define DEFAULT_PORT 8080
#define DEFAULT_IP "127.0.0.1"
#define MAX_PAYLOAD 8192

// [TLV 명시적 통신 헤더]
typedef struct {
    uint32_t code;   // 클라이언트: CMD, 서버: Result Code
    uint32_t length; // 뒤따라오는 문자열 Payload의 바이트 길이
} NetHeader;

typedef struct {
    char name[50];
    int qty;
} CartItem;

static inline void clear_screen(void) {
    printf("\033[2J\033[H");
}

static void flush_stdin(void) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

int get_safe_input(char *buffer, size_t size) {
    if (fgets(buffer, size, stdin) == NULL) return -1;
    char *newline = strchr(buffer, '\n');
    if (newline) *newline = '\0';
    else flush_stdin(); 
    return 0;
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
    char temp[2]; get_safe_input(temp, sizeof(temp));
}

// TCP 파편화 대응: 지정 길이 완전 송신 보장
ssize_t send_exact(int sock, const void *buf, size_t len) {
    size_t total_sent = 0;
    const char *p = (const char *)buf;
    while (total_sent < len) {
        ssize_t n = send(sock, p + total_sent, len - total_sent, 0);
        if (n <= 0) return -1; 
        total_sent += n;
    }
    return total_sent;
}

// TCP 파편화 대응: 지정 길이 완전 수신 보장
ssize_t recv_exact(int sock, void *buf, size_t len) {
    size_t total_recv = 0;
    char *p = (char *)buf;
    while (total_recv < len) {
        ssize_t n = recv(sock, p + total_recv, len - total_recv, 0);
        if (n <= 0) return -1;
        total_recv += n;
    }
    return total_recv;
}

// 직렬화 통신 핵심 래퍼 함수 (헤더/페이로드 분리 및 엔디안 처리)
int request_server(int sock, uint32_t cmd, const char* payload_out, char* msg_in, int* total_pages) {
    NetHeader req_hdr, res_hdr;
    char payload_in[MAX_PAYLOAD];
    uint32_t out_len = payload_out ? strlen(payload_out) : 0;
    
    req_hdr.code = htonl(cmd);
    req_hdr.length = htonl(out_len);
    
    // 헤더 및 페이로드 전송
    if (send_exact(sock, &req_hdr, sizeof(NetHeader)) < 0) return -1;
    if (out_len > 0 && send_exact(sock, payload_out, out_len) < 0) return -1;

    // 헤더 수신
    if (recv_exact(sock, &res_hdr, sizeof(NetHeader)) < 0) return -1;
    uint32_t res_code = ntohl(res_hdr.code);
    uint32_t in_len = ntohl(res_hdr.length);
    
    if (in_len > MAX_PAYLOAD - 1) return -1;
    
    // 페이로드 수신 및 안전한 종료
    if (in_len > 0) {
        if (recv_exact(sock, payload_in, in_len) < 0) return -1;
    }
    payload_in[in_len] = '\0'; 

    // 역직렬화: 서버가 보낸 "total_pages|메시지" 분리
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
    for (int i = 0; str[i] != '\0'; i++) {
        if (str[i] < '0' || str[i] > '9') return 0; 
    }
    return 1;
}

void handle_category_view(int sock, int sum_cmd, int det_cmd, int del_cat_cmd, int del_item_cmd, int del_all_cmd, const char* title) {
    char payload[MAX_PAYLOAD];
    char response[MAX_PAYLOAD];
    int pages = 0;

    while(1) {
        clear_screen();
        if (request_server(sock, sum_cmd, "", response, &pages) < 0) return;
        
        printf("%s\n", response); 
        if (strstr(response, "없습니다") != NULL) { pause_screen(); break; }

        printf("\n[%s] 상세조회할 '상품명'을 정확히 입력하세요.\n", title);
        if (del_all_cmd != 0) printf("('all': 등록된 전체 일괄 삭제, 뒤로가기: '0') => ");
        else printf("(뒤로가기: '0') => ");
        
        char cat_name[50]; 
        get_safe_input(cat_name, sizeof(cat_name));

        if (strcmp(cat_name, "0") == 0) break; 
        else if (del_all_cmd != 0 && strcmp(cat_name, "all") == 0) {
            if (request_server(sock, del_all_cmd, "", response, &pages) < 0) return;
            printf("\n[서버 응답] %s\n", response); 
            pause_screen(); continue; 
        }

        int current_page = 1; 
        while(1) {
            clear_screen();
            // 직렬화 전송: "카테고리명|페이지번호"
            snprintf(payload, sizeof(payload), "%s|%d", cat_name, current_page);
            if (request_server(sock, det_cmd, payload, response, &pages) < 0) return;
            
            printf("%s\n", response); 
            if(strstr(response, "해당") != NULL && strstr(response, "없습니다") != NULL) { 
                pause_screen(); break; 
            }

            printf("\n---------------------------------\n");
            printf("  - 상품 ID : 해당 상품만 지정 삭제\n");
            printf("  - 'all'   : 현재 조회 중인 [%s] 카테고리 전체 삭제\n", cat_name);
            printf("  - 숫자    : 해당 페이지로 즉시 이동\n");
            printf("  - '0'     : 뒤로가기\n");
            printf("---------------------------------\n입력: ");
            
            char input[50]; 
            get_safe_input(input, sizeof(input));

            if (strcmp(input, "0") == 0) break; 
            else if (strcmp(input, "all") == 0) {
                if (request_server(sock, del_cat_cmd, cat_name, response, &pages) < 0) return;
                printf("\n[서버 응답] %s\n", response); 
                pause_screen(); break; 
            }
            else if (is_numeric(input)) {
                int page_num;
                if (parse_int(input, &page_num) && page_num >= 1 && page_num <= pages) current_page = page_num;
                else { printf("\n[오류] 존재하는 페이지 번호를 입력해주세요. (1 ~ %d)\n", pages); pause_screen(); }
            }
            else { 
                if (request_server(sock, del_item_cmd, input, response, &pages) < 0) return;
                printf("\n[서버 응답] %s\n", response); 
                pause_screen(); 
            }
        } 
    }
}

int main(int argc, char *argv[]) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    
    const char* server_ip = (argc > 1) ? argv[1] : DEFAULT_IP;
    int server_port = (argc > 2) ? atoi(argv[2]) : DEFAULT_PORT;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("소켓 생성 실패\n"); return -1;
    }

    struct timeval tv;
    tv.tv_sec = 5;  
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip, &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("서버(%s:%d) 연결 실패.\n", server_ip, server_port);
        return -1;
    }

    printf("서버 연결 성공!\n");
    sleep(1);

    char input_buf[100];
    char req_payload[MAX_PAYLOAD];
    char res_payload[MAX_PAYLOAD];
    int pages = 0;

    while(1) {
        clear_screen();
        printf("\n=================================\n");
        printf("        스마트 재고 관리        \n");
        printf("=================================\n");
        printf("1. 수동 단일 입고\n");
        printf("2. 랜덤 상품 대량 입고 (10종)\n");
        printf("3. 전체 재고 조회 및 삭제 (카테고리별)\n");
        printf("4. 만료 상품 조회 및 삭제 (카테고리별)\n"); 
        printf("5. 상품 판매 (장바구니 결제)\n");
        printf("0. 프로그램 종료\n");
        printf("---------------------------------\n");
        printf("선택: ");
        
        get_safe_input(input_buf, sizeof(input_buf));
        int choice;
        if (!parse_int(input_buf, &choice)) continue;

        if (choice == 1) {
            char id[20], name[50]; int valid_hours;
            printf("상품 ID (예: Z_01): "); get_safe_input(id, sizeof(id));
            printf("상품명: "); get_safe_input(name, sizeof(name));
            printf("유효기간(시간): "); get_safe_input(input_buf, sizeof(input_buf));
            
            if (!parse_int(input_buf, &valid_hours) || valid_hours <= 0) {
                printf("[오류] 유효기간은 1시간 이상이어야 합니다.\n"); pause_screen(); continue;
            }
            
            // 직렬화 송신
            snprintf(req_payload, sizeof(req_payload), "%s|%s|%d", id, name, valid_hours);
            if (request_server(sock, 1, req_payload, res_payload, &pages) < 0) break;
            
            printf("\n[서버 응답] %s\n", res_payload);
            pause_screen(); 
        } 
        else if (choice == 2) {
            printf("몇 개의 랜덤 상품을 입고하시겠습니까?: ");
            get_safe_input(input_buf, sizeof(input_buf));
            int qty;
           if (!parse_int(input_buf, &qty) || qty <= 0 || qty > 500000) {
            printf("[오류] 1개에서 50000개 사이로 입력해주세요.\n"); 
             pause_screen(); 
            continue;
             }


            snprintf(req_payload, sizeof(req_payload), "%d", qty);
            if (request_server(sock, 2, req_payload, res_payload, &pages) < 0) break;
            printf("\n[서버 응답] %s\n", res_payload);
            pause_screen(); 
        }
        else if (choice == 5) {
            CartItem cart[20];
            int cart_size = 0;

            while(1) {
                clear_screen();
                if (request_server(sock, 15, "", res_payload, &pages) < 0) goto connection_lost;
                printf("%s\n", res_payload);

                printf("\n=== 🛒 장바구니 현황 ===\n");
                if (cart_size == 0) printf(" [장바구니가 비어있습니다]\n");
                else {
                    for(int i=0; i<cart_size; i++) printf(" %d. %s - %d개\n", i+1, cart[i].name, cart[i].qty);
                }
                
                printf("---------------------------------\n");
                printf("담을 '상품명' 입력 (결제: 'pay', 취소: '0'): ");
                char input[50]; get_safe_input(input, sizeof(input));

                if (strcmp(input, "0") == 0) {
                    printf("\n장바구니를 비우고 메뉴로 돌아갑니다.\n"); pause_screen(); break;
                }
                else if (strcmp(input, "pay") == 0) {
                    if (cart_size == 0) {
                        printf("\n장바구니가 비어있습니다!\n"); pause_screen(); continue;
                    }
                    clear_screen();
                    printf("\n=== 🧾 영수증 (결제 결과) ===\n\n");
                    for(int i=0; i<cart_size; i++) {
                        snprintf(req_payload, sizeof(req_payload), "%s|%d", cart[i].name, cart[i].qty);
                        if (request_server(sock, 14, req_payload, res_payload, &pages) < 0) goto connection_lost;
                        printf("%s\n", res_payload); 
                    }
                    printf("\n=================================\n");
                    pause_screen(); break;
                }
                else {
                    printf("[%s] 몇 개를 담으시겠습니까?: ", input);
                    get_safe_input(input_buf, sizeof(input_buf));
                    int q;
                    if (parse_int(input_buf, &q) && q > 0) {
                        int found = 0;
                        for(int i=0; i<cart_size; i++) {
                            if (strcmp(cart[i].name, input) == 0) {
                                cart[i].qty += q; found = 1; break;
                            }
                        }
                        if (!found) {
                            if (cart_size < 20) {
                                strncpy(cart[cart_size].name, input, 49);
                                cart[cart_size].qty = q; cart_size++;
                            } else { printf("장바구니 종류가 꽉 찼습니다(최대 20종).\n"); pause_screen(); }
                        }
                    } else { printf("올바른 수량을 입력해주세요.\n"); pause_screen(); }
                }
            }
        }
        else if (choice == 3) {
            handle_category_view(sock, 7, 9, 12, 8, 16, "전체 재고"); 
        }
        else if (choice == 4) {
            handle_category_view(sock, 10, 11, 13, 6, 5, "만료 재고"); 
        }
        else if (input_buf[0] == '0') {
            printf("프로그램을 안전하게 종료합니다.\n"); break;
        }
        else {
            printf("잘못된 번호입니다. 다시 선택해주세요.\n"); pause_screen();
        }
    }

connection_lost:
    close(sock);
    return 0;
}
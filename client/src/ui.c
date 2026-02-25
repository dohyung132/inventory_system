#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include "ui.h"
#include "utils.h"

/**
 * @brief 키보드 입력과 서버 연결 상태를 동시에 감시 (I/O Multiplexing)
 * [핵심 로직] 사용자 입력을 무한정 기다리다가 서버가 죽었을 때 프로그램이 멈추는 
 * '블로킹(Blocking)' 현상을 방지하기 위해 select()를 사용하여 다중 감시를 수행합니다.
 * 
 * @param sock 서버 소켓 디스크립터
 * @return int 키보드 입력 시 1, 서버 연결 종료 시 -1
 */
static int check_input_or_disconnect(int sock) {
    fd_set read_fds;
    while(1) {
        FD_ZERO(&read_fds);  // 읽기 이벤트 집합 초기화
        FD_SET(STDIN_FILENO, &read_fds);  // 1. 키보드 표준 입력 감시 등록
        if (sock >= 0) FD_SET(sock, &read_fds);  // 2. 서버 소켓 감시 등록
        
        // 감시할 디스크립터 중 가장 큰 값 + 1 설정
        int max_fd = (sock > STDIN_FILENO) ? sock : STDIN_FILENO;

        // 이벤트가 발생할 때까지 대기 (timeout NULL은 무한 대기)
        int ret = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (ret > 0) {
            // [사례 A] 서버 소켓에 데이터가 있거나 연결이 끊긴 경우
            if (sock >= 0 && FD_ISSET(sock, &read_fds)) {
                char c;
                // MSG_PEEK: 데이터를 실제로 읽지 않고 확인만 함
                // MSG_DONTWAIT: 즉시 리턴하여 블로킹 방지
                int n = recv(sock, &c, 1, MSG_PEEK | MSG_DONTWAIT);
                if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                    return -1; // 서버가 연결을 끊었거나 에러 발생 시 즉시 감지
                }
            }
            // [사례 B] 사용자가 키보드로 무언가 입력한 경우
            if (FD_ISSET(STDIN_FILENO, &read_fds)) {
                return 1; // 키보드 입력이 감지되면 1 반환
            }
        }
    }
}

/**
 * @brief 터미널 화면 초기화
 * ANSI 이스케이프 코드를 사용하여 화면을 비우고 커서를 좌측 상단으로 이동시킵니다.
 */
void clear_screen(void) {
    printf("\033[2J\033[1;1H");  // 화면 초기화 및 커서 위치 변경
    fflush(stdout);  // 출력 버퍼를 즉시 화면에 반영
}

/**
 * @brief 화면 일시 정지 및 진행 확인
 * [보안] 단순히 Enter를 기다리는 동안에도 서버 상태를 감시하여 갇힘 현상을 방지합니다.
 * 
 * @param sock 서버 소켓 디스크립터
 * @return int 정상 0, 서버 단절 시 -1
 */
int pause_screen(int sock) {
    printf("\n엔터를 누르면 계속합니다...");
    fflush(stdout);
    // 입력 대기 중 서버 단절 시 즉시 에러 반환
    if (check_input_or_disconnect(sock) < 0) return -1;
    clear_input_buffer();  // 입력 버퍼 정리
    return 0;
}

/**
 * @brief 메인 메뉴를 그리는 함수
 */
void draw_main_mode_selection(void) {
    clear_screen();
    printf("======================================\n");
    printf("     스마트 편의점 POS 시스템         \n");
    printf("======================================\n");
    printf(" 1. 판매 모드 (POS)\n");
    printf(" 2. 관리자 모드 (Admin)\n");
    printf(" 0. 시스템 종료\n");
    printf("======================================\n");
}

/**
 * @brief 판매 모드 메뉴를 그리는 함수
 * (현재는 통합 화면 로직으로 대체됨)
 * 
 * @param cid 단말기 고유 ID (화면 상단에 표시)
 */
void draw_pos_menu(uint32_t cid) {
    (void)cid; // pos.c의 통합 화면 로직으로 대체됨
}

/**
 * @brief 관리자 전용 메뉴 레이아웃 렌더링
 * 관리자 모드에서 사용되는 메뉴를 출력합니다.
 * 
 * @param cid 단말기 고유 ID (화면 상단에 표시)
 */
void draw_admin_menu(uint32_t cid) {
    clear_screen();
    printf("======================================\n");
    printf("   [관리자 모드] POS-%04d\n", cid);
    printf("======================================\n");
    printf(" [입고]\n");
    printf(" 1. 단일 상품 입고\n");
    printf(" 2. 랜덤 상품 입고\n");
    printf(" [조회 및 관리]\n");
    printf(" 3. 전체 재고 조회 (상세 검색 및 창고 비우기)\n");
    printf(" 4. 만료 재고 조회 (상세 검색 및 일괄 폐기)\n");
    printf(" 0. 메인 화면으로 돌아가기\n");
    printf("======================================\n");
}

/**
 * @brief 시스템 메시지를 출력하는 함수
 * 
 * @param msg 출력할 메시지
 */
void print_system_message(const char* msg) {
    printf("\n%s\n", msg);  // 시스템 메시지 출력
}

/**
 * @brief 서버 상태를 감시하며 정수 입력을 받는 함수
 * 서버 상태를 실시간으로 체크하면서 사용자로부터 정수 입력을 받습니다.
 * 
 * @param sock 서버 소켓 디스크립터
 * @param prompt 사용자에게 보여줄 프롬프트 메시지
 * @param out_val 입력된 정수 값을 저장할 변수
 * @return int 정상 0, 서버 단절 시 -1
 */
int get_int_input(int sock, const char* prompt, int* out_val) {
    printf("%s", prompt);  // 프롬프트 메시지 출력
    fflush(stdout);
    
    // 입력을 받기 전/도중에 서버 상태 실시간 체크
    if (check_input_or_disconnect(sock) < 0) return -1;

    if (scanf("%d", out_val) != 1) *out_val = -1;  // 정수 입력 받기
    clear_input_buffer();  // 남아있는 개행 문자 등 버퍼 정리
    return 0;
}

/**
 * @brief 서버 상태를 감시하며 문자열 입력을 받는 함수
 * 서버 상태를 실시간으로 체크하면서 사용자로부터 문자열 입력을 받습니다.
 * 버퍼 크기를 초과하지 않도록 안전하게 제한합니다.
 * 
 * @param sock 서버 소켓 디스크립터
 * @param prompt 사용자에게 보여줄 프롬프트 메시지
 * @param buffer 입력된 문자열을 저장할 버퍼
 * @param max_len 버퍼 크기
 * @return int 정상 0, 서버 단절 시 -1
 */
int get_string_input(int sock, const char* prompt, char* buffer, int max_len) {
    printf("%s", prompt);  // 프롬프트 메시지 출력
    fflush(stdout);

    if (check_input_or_disconnect(sock) < 0) return -1;  // 서버 상태 감시

    // fgets로 입력을 받아 버퍼 오버플로우를 방지하고 개행 문자 제거
    if (fgets(buffer, max_len, stdin)) remove_newline(buffer);
    return 0;
}
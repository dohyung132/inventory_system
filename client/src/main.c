#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/select.h>
#include "network.h"
#include "ui.h"
#include "pos.h"
#include "utils.h"

// =====================================================================
// [스마트 편의점 POS 클라이언트 메인]
// 서버와의 통신을 유지하면서, 사용자에게 판매/관리자 메뉴를 제공합니다.
// 서버가 갑자기 종료되더라도 튕기지 않고 스스로 재연결하는 기능이 핵심입니다.
// =====================================================================

int main(void) {
    // 1. [안전 장치] 서버가 죽은 상태에서 데이터를 보내면 OS가 클라이언트를 
    // 강제 종료(SIGPIPE)시켜버리므로, 이 시그널을 무시하도록 설정합니다.
    signal(SIGPIPE, SIG_IGN); 

    // 2. [초기화] 단말기 고유 ID 생성 (0 ~ 9999 사이 랜덤)
    srand((unsigned int)time(NULL)); // 랜덤 시드 초기화
    uint32_t cid = rand() % 10000;  // 고유 단말기 ID 생성 (0 ~ 9999 사이)
    
    // 장바구니 데이터 등 로컬 시스템 초기화
    init_pos_system(); // POS 시스템 초기화 (장바구니, 세팅 등)

    // 소켓 초기 상태는 연결되지 않음(-1)으로 설정
    int sock = -1;
    char trash[1024]; // 응답을 버릴 때 사용하는 임시 버퍼

    // 3. [메인 프로그램 루프] 사용자가 시스템 종료(0)를 누를 때까지 무한 반복
    while (1) {
        
        // -------------------------------------------------------------
        // [A. 서버 연결 및 재연결 감지 블록]
        // 소켓이 -1이라면 (처음 켰거나, 통신 중 끊긴 경우) 연결을 시도합니다.
        // -------------------------------------------------------------
        if (sock < 0) {
            clear_screen();
            printf("======================================\n");
            printf(" 서버 연결 대기 중...\n");
            printf("======================================\n");
            
            // 서버와 연결될 때까지 무한 재시도 (블로킹 방지 로직 포함)
            while ((sock = connect_to_server(SERVER_IP, PORT)) < 0) {
                int exit_flag = 0;
                
                // 대기 중 로그 도배를 막기 위해 3초 동안 제자리 카운트다운(새로고침)
                for (int i = 3; i > 0; i--) {
                    printf("\r\033[K[네트워크] 서버 응답 없음. 재연결 대기 중... %d초 (종료: q + Enter)", i);
                    fflush(stdout);
                    
                    // 입력 감시 설정 (1초 동안 사용자가 키보드를 치는지 감시)
                    fd_set read_fds;
                    FD_ZERO(&read_fds);
                    FD_SET(STDIN_FILENO, &read_fds);
                    struct timeval tv;
                    tv.tv_sec = 1; tv.tv_usec = 0; 
                    
                    // 1초 대기 중 입력이 발생하면 처리
                    if (select(STDIN_FILENO + 1, &read_fds, NULL, NULL, &tv) > 0) {
                        char c = getchar();
                        clear_input_buffer();
                        if (c == 'q' || c == 'Q') {
                            exit_flag = 1; // 종료 플래그 활성화
                            break;
                        }
                    }
                }
                
                // 'q'를 눌러 탈출하려는 경우 프로그램 완전 종료
                if (exit_flag) {
                    clear_screen();
                    printf("단말기 시스템을 완전히 종료합니다.\n");
                    exit(0);
                }
            }
            
            // 연결 성공 시 안내 메시지 출력 및 서버에 최초 접속 인사(cmd 99) 전송
            printf("\r\033[K\n[System] 서버 연결 성공!\n");
            pause_screen(-1); // 메시지 확인 대기

            send_request(sock, cid, 99, ""); 
            receive_response(sock, trash, NULL); 
        }

        // -------------------------------------------------------------
        // [B. 메인 메뉴 화면]
        // 사용자가 선택할 수 있는 주요 메뉴를 출력하고, 입력을 받습니다.
        // 서버와의 연결이 끊어진 경우 재연결을 시도합니다.
        // -------------------------------------------------------------
        draw_main_mode_selection();  // 메인 모드 선택 화면 그리기
        
        int choice;
        // 사용자 입력 대기. (get_int_input 내부에서 서버 끊김을 감시함)
        // 리턴값이 0보다 작으면 입력 대기 중 서버가 죽은 것이므로 재연결 블록으로 점프!
        if (get_int_input(sock, "모드 선택 >> ", &choice) < 0) {
            sock = -1;  // 서버와의 연결이 끊어졌으므로 소켓 초기화
            continue;   // 재연결 시도
        }

        // 0 입력 시 메인 루프 탈출 -> 프로그램 정상 종료
        if (choice == 0) break;

        int status = 0;
        
        // 선택한 모드로 진입 (각 모드 내부에서도 에러 발생 시 -1을 리턴함)
        switch (choice) {
            case 1: 
                status = run_pos_mode(sock, cid);  // 판매 모드 실행
                break;
            case 2: 
                status = run_admin_mode(sock, cid);  // 관리자 모드 실행
                break;
            default:
                print_system_message("[오류] 잘못된 입력입니다.");
                pause_screen(sock);
                break;
        }

        // -------------------------------------------------------------
        // [C. 에러 캐스케이딩 처리]
        // 하위 모드(pos/admin)에서 작업 중 서버가 끊겨 -1을 리턴받은 경우
        // -------------------------------------------------------------
        if (status < 0) {
            print_system_message("\n[경고] 서버와의 연결이 끊어졌습니다. (즉시 재연결 시도)");
            disconnect_from_server(sock); // 기존 찌꺼기 소켓 정리
            sock = -1; // 소켓 상태를 초기화하여 다음 루프에서 재연결 유도
            sleep(1);  // 경고 메시지를 사용자가 짧게 인지할 수 있도록 1초 대기
        }
    }

    // 4. [정상 종료 처리] 사용자가 0을 눌러 정상적으로 끝내는 경우
    if (sock >= 0) {
        // 서버에 정상 접속 종료(cmd 100)를 알림
        send_request(sock, cid, 100, ""); 
        receive_response(sock, trash, NULL);
        disconnect_from_server(sock);
    }

    clear_screen();  // 화면 정리
    printf("시스템을 종료합니다.\n");
    return 0;
}
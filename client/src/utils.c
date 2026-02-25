#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "utils.h"

/**
 * @brief 입력 버퍼(stdin)에 남아있는 찌꺼기 데이터를 모두 제거합니다.
 * [필요성] scanf나 fgets 사용 후 버퍼에 남은 개행 문자('\n')나 잘못 입력된 
 * 데이터가 다음 입력 시 영향을 주는 것을 방지하여 프로그램의 오작동을 막습니다.
 */
void clear_input_buffer(void) {
    int c;
    // 버퍼에서 더 이상 읽을 데이터가 없거나 개행 문자를 만날 때까지 반복해서 읽어냄
    while ((c = getchar()) != '\n' && c != EOF);
}

/**
 * @brief 문자열 끝에 포함된 개행 문자('\n')를 제거합니다.
 * [설계 의도] fgets() 함수는 입력 받은 문자열 끝에 '\n'을 포함시키기 때문에, 
 * 서버로 전송하거나 비교 로직에 사용하기 전 이를 제거하여 순수한 데이터만 남깁니다.
 * 
 * @param str 수정할 대상 문자열
 */
void remove_newline(char* str) {
    if (str == NULL) return; // 널 포인터 예외 방지

    size_t len = strlen(str);
    // 문자열이 존재하고 마지막 문자가 개행 문자라면 널 문자로 치환
    if (len > 0 && str[len - 1] == '\n') {
        str[len - 1] = '\0';
    }
}

/**
 * @brief 화면 전환 전 사용자가 내용을 인지할 수 있도록 일시 정지합니다.
 * (이 함수는 ui.c의 pause_screen과 협력하여 동작하며, 로직에 따라 재구성될 수 있습니다.)
 */
void pause_screen_simple(void) {
    printf("\n엔터를 누르면 계속합니다...");
    fflush(stdout);
    clear_input_buffer(); // 입력 버퍼에 남아 있는 데이터를 모두 지움
}

/**
 * @brief 문자열이 숫자로만 구성되어 있는지 검증합니다.
 * [안전성] atoi() 호출 전 이 함수를 통해 검증하면 잘못된 문자 유입으로 인한 
 * 데이터 왜곡을 방지할 수 있습니다.
 * 
 * @param str 검증할 문자열
 * @return int 모두 숫자면 1, 아니면 0
 */
int is_numeric(const char* str) {
    if (str == NULL || *str == '\0') return 0; // 빈 문자열은 숫자가 아님
    
    while (*str) {
        // 현재 문자가 숫자가 아니라면 즉시 거짓 반환
        if (*str < '0' || *str > '9') return 0;
        str++;
    }
    return 1; // 모든 문자가 숫자이면 1 반환
}
#ifndef POS_H
#define POS_H

#include <stdint.h>

// [장바구니 아이템 구조체]
typedef struct {
    char name[50];
    int qty;
} CartItem;

// 시스템 초기화
void init_pos_system(void);

// 메인 실행 루프 (정상: 0, 통신에러: -1 반환)
int run_pos_mode(int sock, uint32_t cid);
int run_admin_mode(int sock, uint32_t cid);

#endif // POS_H
#ifndef UI_H
#define UI_H

#include <stdint.h>

void clear_screen(void);
int pause_screen(int sock);

void draw_main_mode_selection(void);
void draw_pos_menu(uint32_t cid);
void draw_admin_menu(uint32_t cid);

void print_system_message(const char* msg);

// 소켓 감시 기능이 추가된 입력 함수 (에러시 -1 반환)
int get_int_input(int sock, const char* prompt, int* out_val);
int get_string_input(int sock, const char* prompt, char* buffer, int max_len);

#endif // UI_H
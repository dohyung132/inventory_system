#ifndef INVENTORY_H
#define INVENTORY_H

#include <stdint.h>
#include <time.h>

// 시스템 초기화 및 DB 관리
void init_inventory(void);
void load_data(void);
void save_data(void);
void free_all_resources(void);
void clear_inventory_db(void);

// 비즈니스 로직 API (네트워크 계층에서 호출)
void handle_single_import(uint32_t cid, char* pin, char* msg);
void handle_random_import(uint32_t cid, char* pin, char* msg);
void handle_sell(uint32_t cid, char* pin, char* msg);
void handle_cart_verify(char* pin, char* msg);
void handle_delete_operations(uint32_t cmd, uint32_t cid, char* pin, char* msg);

// 조회 및 포맷팅 로직
void make_category_summary(char* out, int mode, const char* title);
int make_detail_page(char* out, const char* name, int page, int mode);

// 만료 모니터링 API
int check_and_update_expirations(time_t current_vt);
void recover_missed_expirations(time_t current_vt);

#endif // INVENTORY_H
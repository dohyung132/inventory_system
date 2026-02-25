#ifndef LOGGER_H
#define LOGGER_H

#include <time.h>

// 로그 관리
void load_persistent_logs(void);
void clear_persistent_logs(void);
void update_log(const char* msg);

// UI 관리
void draw_dashboard(const char* time_str);
void browse_logs(int start_page);

// 관리자 콘솔 스레드
void* admin_console_thread(void* arg);

#endif // LOGGER_H
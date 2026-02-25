#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "utils.h"
#include "logger.h"

#define CONFIG_FILE "server_config.txt"

// 내부 상태 (캡슐화됨)
static int current_server_mode = 1;
static int current_speed_factor = 1;
static int is_clock_visible = 1;
static time_t start_real_time;
static time_t start_virtual_time;

char db_filename[50] = "oper_db.txt";
char log_filename[50] = "oper_server.log";

void init_config(int mode) {
    current_server_mode = mode;
    current_speed_factor = 1;
    
    if (mode == 2) {
        strncpy(db_filename, "sim_db.txt", sizeof(db_filename)-1);
        strncpy(log_filename, "sim_server.log", sizeof(log_filename)-1);
    } else {
        strncpy(db_filename, "oper_db.txt", sizeof(db_filename)-1);
        strncpy(log_filename, "oper_server.log", sizeof(log_filename)-1);
    }
    
    time(&start_real_time);
    start_virtual_time = start_real_time;
}

void load_config(void) {
    if (current_server_mode != 2) return;

    FILE *fp = fopen(CONFIG_FILE, "r");
    if (!fp) { current_speed_factor = 1; return; }

    int saved_speed;
    long saved_vt, saved_rt;
    if (fscanf(fp, "%d %ld %ld", &saved_speed, &saved_vt, &saved_rt) == 3) {
        current_speed_factor = saved_speed;
        time_t now; time(&now);
        time_t time_diff = now - (time_t)saved_rt; 
        time_t resumed_vt = (time_t)saved_vt + (time_diff * current_speed_factor);
        
        start_real_time = now;
        start_virtual_time = resumed_vt;

        char msg[256];
        snprintf(msg, sizeof(msg), "[System] 세션 복구 완료 (%ld초 흐름, 배속: x%d)", time_diff, current_speed_factor);
        update_log(msg);
    }
    fclose(fp);
}

void save_config(void) {
    if (current_server_mode != 2) return; 
    FILE *fp = fopen(CONFIG_FILE, "w");
    if (!fp) return;
    
    time_t now; time(&now);
    time_t current_vt = get_virtual_time();
    fprintf(fp, "%d %ld %ld\n", current_speed_factor, (long)current_vt, (long)now);
    fclose(fp);
}

int get_server_mode(void) { return current_server_mode; }
int get_speed_factor(void) { return current_speed_factor; }

void set_speed_factor(int new_speed) {
    time_t now; time(&now);
    start_virtual_time = start_virtual_time + (time_t)(difftime(now, start_real_time) * current_speed_factor);
    start_real_time = now;
    current_speed_factor = new_speed;
}

int is_clock_showing(void) { return is_clock_visible; }
void set_clock_showing(int show) { is_clock_visible = show; }

time_t get_virtual_time(void) {
    time_t now; time(&now);
    return start_virtual_time + (time_t)(difftime(now, start_real_time) * current_speed_factor);
}

void reset_virtual_time(void) {
    time(&start_real_time);
    start_virtual_time = start_real_time;
}

void print_time_str(time_t t, char* buf) {
    struct tm tm_info; localtime_r(&t, &tm_info);
    strftime(buf, 26, "%Y-%m-%d %H:%M:%S", &tm_info);
}
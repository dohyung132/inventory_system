# 컴파일러 및 컴파일 옵션 설정
CC = gcc
CFLAGS = -Wall -Wextra -Iinclude -pthread

# 디렉토리 설정
SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj

# 타겟 실행 파일 이름
TARGET = server

# 소스 파일 및 오브젝트 파일 목록 자동 생성
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

# 기본 타겟 (make 명령어 입력 시 실행됨)
all: $(TARGET)

# 최종 실행 파일 링킹
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# 개별 소스 파일을 오브젝트 파일로 컴파일
# (obj 폴더가 없으면 먼저 생성)
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# obj 디렉토리 생성 규칙
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# 빌드 산출물 지우기 (make clean)
clean:
	rm -rf $(OBJ_DIR) $(TARGET)

# 파일 이름과 타겟 이름이 겹치는 것 방지
.PHONY: all clean
CC = gcc
CFLAGS = -Wall -Iinclude
BIN_DIR = bin

# 최종 타겟: 클라이언트와 서버 두 개를 만듭니다.
all: $(BIN_DIR)/client $(BIN_DIR)/server

# 클라이언트 빌드
$(BIN_DIR)/client: src/main.c
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ src/main.c

# 서버 빌드
$(BIN_DIR)/server: src/server.c
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ src/server.c

clean:
	rm -rf $(BIN_DIR)/*.o $(BIN_DIR)/client $(BIN_DIR)/server

.PHONY: all clean

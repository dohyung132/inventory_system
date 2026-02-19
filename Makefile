CC = gcc
CFLAGS = -Wall -Iinclude
# 최종 실행 파일 이름
TARGET = bin/inventory_system

# src 폴더 안의 모든 .c 파일을 재료로 사용하도록 수정
SRCS = src/main.c src/server.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

# 개별 .c 파일을 .o 파일로 만드는 규칙
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f src/*.o $(TARGET)

.PHONY: all clean

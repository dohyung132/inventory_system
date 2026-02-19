CC = gcc
CFLAGS = -Wall -Iinclude
TARGET = bin/inventory_system
SRCS = src/main.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

clean:
	rm -f src/*.o $(TARGET)
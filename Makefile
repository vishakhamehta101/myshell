# Makefile for myshell

CC      ?= cc
CFLAGS  ?= -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -g
TARGET  := myshell
SRC     := src/myshell.c

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

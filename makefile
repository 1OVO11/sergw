CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -g -O0 \
          -fsanitize=address,undefined -fno-omit-frame-pointer
TARGET  = sergw
OBJS    = main.o parser.o serial.o

.PHONY: all clean test unit

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

main.o: main.c parser.h serial.h
	$(CC) $(CFLAGS) -c -o $@ main.c

parser.o: parser.c parser.h
	$(CC) $(CFLAGS) -c -o $@ parser.c

serial.o: serial.c serial.h
	$(CC) $(CFLAGS) -c -o $@ serial.c

clean:
	rm -f $(OBJS) $(TARGET) test_units test.bin

test: $(TARGET)
	python3 gen_test.py
	./$(TARGET) -f test.bin

unit: parser.o
	$(CC) $(CFLAGS) -I. -o test_units test_units.c parser.o
	./test_units
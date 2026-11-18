CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -g -O0 \
          -fsanitize=address,undefined -fno-omit-frame-pointer
TARGET  = parser
OBJS    = main.o parser.o

.PHONY: all clean test crc

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

main.o: main.c parser.h
	$(CC) $(CFLAGS) -c -o $@ main.c

parser.o: parser.c parser.h
	$(CC) $(CFLAGS) -c -o $@ parser.c

clean:
	rm -f $(OBJS) $(TARGET)

test: $(TARGET)
	python3 gen_test.py
	./$(TARGET) test.bin

.PHONY: all clean test unit

unit: parser.o
	$(CC) $(CFLAGS) -I. -o test_units test_units.c parser.o
	./test_units

clean:
	rm -f $(OBJS) $(TARGET) test_units test.bin
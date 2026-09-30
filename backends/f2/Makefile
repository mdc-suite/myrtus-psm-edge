CC = gcc
CFLAGS = -c -fPIC
SOURCE_DIR = src
BUILD_DIR = build

all: $(BUILD_DIR) aes128

$(BUILD_DIR):
	mkdir -pv $(BUILD_DIR)

aes128.o:
	$(CC) $(CFLAGS) a.c -o aes128.o

clean:
	rm -rf *.o

.PHONY: all clean

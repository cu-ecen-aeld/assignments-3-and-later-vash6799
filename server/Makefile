CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -Wall -Werror -g
TARGET = aesdsocket
LDFLAGS ?= -lpthread

all: $(TARGET)

$$(TARGET): aesdsocket.c
	$(CC) $(CFLAGS) aesdsocket.c -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET) *.o
CC=gcc
CFLAGS=-Wall -Wextra -Werror -g
CROSS_COMPILE ?=
TARGET=$(CROSS_COMPILE)gcc
OUTPUT=aesdsocket
SRC=aesdsocket.c

all: $(OUTPUT)

default: all

$(OUTPUT): $(SRC)
	$(TARGET) $(CFLAGS) -o $@ $^

clean:
	rm -f $(OUTPUT)

.PHONY: all default clean
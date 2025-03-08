CC=$(TARGET)
CFLAGS=-Wall -Wextra -Werror -g
CROSS_COMPILE ?=
TARGET=$(CROSS_COMPILE)gcc
OUTPUT=aesdsocket
SRC=aesdsocket.c

all: $(OUTPUT)

default: all

$(OUTPUT): $(SRC)
        $(CC) $(CFLAGS) -o $@ $^
clean:
        rm -f $(OUTPUT)

.PHONY: all default clean
CC=$(CROSS_COMPILE)gcc
CFLAGS=-Wall -Wextra -Werror -g
OUTPUT=server/aesdsocket
SRC=server/aesdsocket.c

all: $(OUTPUT)

default: all

$(OUTPUT): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(OUTPUT)

.PHONY: all default clean
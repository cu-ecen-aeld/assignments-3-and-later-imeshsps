CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -Wall -Wextra -g
LDFLAGS ?= 
LIBS ?= -lpthread -lrt

OUTPUT = aesdsocket
SRC = aesdsocket.c

all: $(OUTPUT)

default: all

$(OUTPUT): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

clean:
	rm -f $(OUTPUT)

.PHONY: all default clean
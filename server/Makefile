# Makefile for aesdsocket
CC ?= gcc
CFLAGS ?= -Wall -Werror -g -std=gnu99
LDFLAGS ?=
LIBS = -lpthread

# Set USE_AESD_CHAR_DEVICE to 1 by default as per assignment requirements
USE_AESD_CHAR_DEVICE ?= 1
ifeq ($(USE_AESD_CHAR_DEVICE),1)
CFLAGS += -DUSE_AESD_CHAR_DEVICE=1
endif

TARGET = aesdsocket
SOURCES = aesdsocket.c
OBJECTS = $(SOURCES:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $(TARGET) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: all clean
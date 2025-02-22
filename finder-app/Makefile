# Makefile for writer application

# Compiler settings
CC := $(CROSS_COMPILE)gcc
CFLAGS := -Wall -Werror

# Target binary
TARGET := writer

# Default target: build the writer application
all: $(TARGET)

# Build the writer application
$(TARGET): writer.o
	$(CC) $(CFLAGS) -o $(TARGET) writer.o

# Compile object file
writer.o: writer.c
	$(CC) $(CFLAGS) -c writer.c

# Clean target to remove generated files
clean:
	rm -f $(TARGET) *.o

# PHONY declarations to prevent conflicts with files
.PHONY: default clean


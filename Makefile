# Compiler
CC = gcc

# Target executable name
TARGET = jane-ui

# Source files
SRC = main.c

# Libraries to link
LIBS = -lSDL2 -lSDL2_mixer -lmicrohttpd -lm -lpthread

# Compiler flags (add any additional flags here)
CFLAGS = -Wall -Wextra -pedantic

# Default target
all: $(TARGET)

# Link the executable
$(TARGET): $(SRC)
    $(CC) $(CFLAGS) -o $@ $^ $(LIBS)

# Clean up build artifacts
clean:
    rm -f $(TARGET)

.PHONY: all clean

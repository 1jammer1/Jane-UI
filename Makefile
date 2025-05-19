# Compiler
CC = gcc

# Target executable name
TARGET = jane-ui

# Source files
SRC = main.c

# Object files
OBJ = $(SRC:.c=.o)

# Libraries to link
LIBS = -lSDL2 -lSDL2_mixer -lmicrohttpd -lm -lpthread -lcurl

# Compiler flags
CFLAGS = -Wall -Wextra -pedantic

# Default target
all: $(TARGET)

# Link the executable
$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ) $(LIBS)

# Compile source files to object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up build artifacts
clean:
	rm -f $(TARGET) $(OBJ)

.PHONY: all clean

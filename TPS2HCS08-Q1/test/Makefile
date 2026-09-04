#===============================================================================
# Makefile for TPS2HCS08-Q1 Unit Tests
# Requires Unity test framework: https://github.com/ThrowTheSwitch/Unity
#===============================================================================

# Compiler
CC = gcc

# Directories
UNITY_DIR = Unity/src
SRC_DIR = ..
TEST_DIR = TPS2HCS08
MOCK_DIR = mocks
BUILD_DIR = build

# Include paths
INCLUDES = -I$(UNITY_DIR) \
           -I$(SRC_DIR) \
           -I$(MOCK_DIR) \
           -I.

# Compiler flags
CFLAGS = -Wall -Wextra -std=c99 -g $(INCLUDES)

# Define to expose internal symbols for testing
CFLAGS += -DD_STATIC=

# Source files
SRC_FILES = $(SRC_DIR)/ExVioDb_Tps2hcs08.c

# Mock files
MOCK_FILES = $(MOCK_DIR)/mock_SPI.c \
             $(MOCK_DIR)/mock_Logger.c

# Test files
TEST_FILES = $(TEST_DIR)/test_TPS2HCS08_Init.c \
             $(TEST_DIR)/test_TPS2HCS08_Retry.c \
             $(TEST_DIR)/test_TPS2HCS08_SetGetPort.c \
             $(TEST_DIR)/test_TPS2HCS08_Bitfield.c \
             $(TEST_DIR)/test_runner.c

# Unity source
UNITY_SRC = $(UNITY_DIR)/unity.c

# Object files
OBJS = $(BUILD_DIR)/ExVioDb_Tps2hcs08.o \
       $(BUILD_DIR)/mock_SPI.o \
       $(BUILD_DIR)/mock_Logger.o \
       $(BUILD_DIR)/test_TPS2HCS08_Init.o \
       $(BUILD_DIR)/test_TPS2HCS08_Retry.o \
       $(BUILD_DIR)/test_TPS2HCS08_SetGetPort.o \
       $(BUILD_DIR)/test_TPS2HCS08_Bitfield.o \
       $(BUILD_DIR)/test_runner.o \
       $(BUILD_DIR)/unity.o

# Target executable
TARGET = $(BUILD_DIR)/test_tps2hcs08

#===============================================================================
# Build Rules
#===============================================================================

.PHONY: all clean test setup

all: setup $(TARGET)

# Create build directory
setup:
	@mkdir -p $(BUILD_DIR)

# Link all objects
$(TARGET): $(OBJS)
	@echo "Linking $@..."
	@$(CC) $(CFLAGS) -o $@ $^
	@echo "Build complete: $@"

# Compile source file
$(BUILD_DIR)/ExVioDb_Tps2hcs08.o: $(SRC_DIR)/ExVioDb_Tps2hcs08.c
	@echo "Compiling $<..."
	@$(CC) $(CFLAGS) -c $< -o $@

# Compile mock files
$(BUILD_DIR)/mock_SPI.o: $(MOCK_DIR)/mock_SPI.c
	@echo "Compiling $<..."
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/mock_Logger.o: $(MOCK_DIR)/mock_Logger.c
	@echo "Compiling $<..."
	@$(CC) $(CFLAGS) -c $< -o $@

# Compile test files
$(BUILD_DIR)/test_TPS2HCS08_Init.o: $(TEST_DIR)/test_TPS2HCS08_Init.c
	@echo "Compiling $<..."
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/test_TPS2HCS08_Retry.o: $(TEST_DIR)/test_TPS2HCS08_Retry.c
	@echo "Compiling $<..."
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/test_TPS2HCS08_SetGetPort.o: $(TEST_DIR)/test_TPS2HCS08_SetGetPort.c
	@echo "Compiling $<..."
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/test_TPS2HCS08_Bitfield.o: $(TEST_DIR)/test_TPS2HCS08_Bitfield.c
	@echo "Compiling $<..."
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/test_runner.o: $(TEST_DIR)/test_runner.c
	@echo "Compiling $<..."
	@$(CC) $(CFLAGS) -c $< -o $@

# Compile Unity framework
$(BUILD_DIR)/unity.o: $(UNITY_DIR)/unity.c
	@echo "Compiling Unity..."
	@$(CC) $(CFLAGS) -c $< -o $@

# Run tests
test: $(TARGET)
	@echo ""
	@echo "========================================="
	@echo "Running TPS2HCS08 Unit Tests..."
	@echo "========================================="
	@./$(TARGET)
	@echo ""

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR)
	@echo "Clean complete."

#===============================================================================
# Help
#===============================================================================

help:
	@echo "TPS2HCS08 Unit Test Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  make all    - Build test executable"
	@echo "  make test   - Build and run tests"
	@echo "  make clean  - Remove build artifacts"
	@echo "  make help   - Show this help"
	@echo ""
	@echo "Prerequisites:"
	@echo "  1. Install Unity framework:"
	@echo "     cd test && git clone https://github.com/ThrowTheSwitch/Unity.git"
	@echo "  2. Ensure gcc is in PATH"
	@echo ""

.PHONY: build run clean rebuild help build-server build-client run-server run-client

# Default build directory
BUILD_DIR := build

# Executable name
EXECUTABLE := mdbms_client

# C++ compiler
CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -pthread

build:
	@echo "Building project..."
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake .. && cmake --build .
	@echo "Build complete! Executable: $(BUILD_DIR)/src/$(EXECUTABLE)"

run: build
	@echo "Running $(EXECUTABLE)..."
	@$(BUILD_DIR)/src/$(EXECUTABLE)

# Build server executable (using CMake)
build-server:
	@echo "Building server..."
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake .. && cmake --build . --target server
	@ln -sf src/server $(BUILD_DIR)/server
	@echo "Server built successfully! Executable: $(BUILD_DIR)/server"

# Build client executable (using CMake)
build-client:
	@echo "Building client..."
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake .. && cmake --build . --target client
	@ln -sf src/client $(BUILD_DIR)/client
	@echo "Client built successfully! Executable: $(BUILD_DIR)/client"

# Build both server and client
build-cs: build-server build-client
	@echo "Server and client built successfully!"

# Run server (in background)
run-server: build-server
	@echo "Starting server..."
	@$(BUILD_DIR)/server

# Run client
run-client: build-client
	@echo "Starting client..."
	@$(BUILD_DIR)/client

clean:
	@echo "Cleaning build directory..."
	@rm -rf $(BUILD_DIR)
	@rm -f server client
	@echo "Clean complete!"

rebuild: clean build
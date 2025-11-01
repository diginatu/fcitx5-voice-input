# Makefile for fcitx5-voice-input

.PHONY: all build clean install

# Default target: clean, configure, and build the project.
# This is equivalent to the command you provided.
all: build

build:
	@echo "Building..."
	@rm -fr build && mkdir build && cd build && cmake -G Ninja .. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_EXPORT_COMPILE_COMMANDS=1 && ninja

# Clean the build directory
clean:
	@echo "Cleaning..."
	@rm -rf build

# Install the project.
# Note: This will install to /usr, which requires root privileges.
install:
	@echo "Installing..."
	cd build && sudo ninja install

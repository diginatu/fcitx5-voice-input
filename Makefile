# Makefile for fcitx5-voice-input

.PHONY: all build clean install test build-release publish

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

# Run tests (build first if needed)
test:
	@if [ ! -d build ]; then $(MAKE) build; fi
	@echo "Running tests..."
	cd build && ctest --output-on-failure

# Optimized release build (no debug info)
build-release:
	@echo "Building release..."
	@rm -fr build && mkdir build && cmake -B build -S . \
		-G Ninja \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX=/usr \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=0 && \
	cmake --build build

# Bump pkgver in aur/PKGBUILD to current commit and push to trigger AUR publish
publish:
	$(eval PKGVER := r$(shell git rev-list --count HEAD).$(shell git rev-parse --short HEAD))
	@echo "Publishing $(PKGVER) to AUR..."
	@sed -i "s/^pkgver=.*/pkgver=$(PKGVER)/" aur/PKGBUILD
	@git add aur/PKGBUILD
	@git commit -m "Update pkgver to $(PKGVER)"
	@git push

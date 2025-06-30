# Eidon Glove Root Makefile
# Forwards commands to the firmware directory

.PHONY: all build clean flash monitor menuconfig fullclean help

# Default target
all: build

build:
	@echo "Building firmware..."
	@cd firmware && source ~/esp-idf/export.sh && idf.py build

clean:
	@echo "Cleaning build files..."
	@cd firmware && source ~/esp-idf/export.sh && idf.py clean

fullclean:
	@echo "Full clean..."
	@cd firmware && source ~/esp-idf/export.sh && idf.py fullclean

flash:
	@echo "Flashing firmware..."
	@cd firmware && source ~/esp-idf/export.sh && idf.py flash

monitor:
	@echo "Starting serial monitor..."
	@cd firmware && source ~/esp-idf/export.sh && idf.py monitor

menuconfig:
	@echo "Opening menuconfig..."
	@cd firmware && source ~/esp-idf/export.sh && idf.py menuconfig

# Combined commands
flash-monitor: flash monitor

build-flash: build flash

build-flash-monitor: build flash monitor

# Help target
help:
	@echo "Eidon Glove ESP-IDF Build System"
	@echo "================================"
	@echo "Available targets:"
	@echo "  make build              - Build the firmware"
	@echo "  make flash              - Flash the firmware to device"
	@echo "  make monitor            - Open serial monitor"
	@echo "  make clean              - Clean build files"
	@echo "  make fullclean          - Full clean (remove all build artifacts)"
	@echo "  make menuconfig         - Open ESP-IDF configuration menu"
	@echo "  make flash-monitor      - Flash and then monitor"
	@echo "  make build-flash        - Build and flash"
	@echo "  make build-flash-monitor - Build, flash, and monitor"
	@echo ""
	@echo "You can also use ./idf.py directly with any ESP-IDF command" 
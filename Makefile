# Makefile for nfs sync system

# === CONFIG ===
SRC_DIR := src
CONFIG_DIR := config
CONFIG_FILE := $(CONFIG_DIR)/config.txt

CLIENT1_DIR := client1_dir
CLIENT2_DIR := client2_dir

MANAGER := nfs_manager
CLIENT := nfs_client
CONSOLE := nfs_console

MANAGER_SRC := $(SRC_DIR)/nfs_manager.c
CLIENT_SRC := $(SRC_DIR)/nfs_client.c
CONSOLE_SRC := $(SRC_DIR)/nfs_console.c

MANAGER_LOG := manager.log

CC := gcc
CFLAGS := -pthread -O2

# === DEFAULT TARGET ===
.PHONY: all
all: $(MANAGER) $(CLIENT) $(CONSOLE)

# === BUILD TARGETS ===
$(MANAGER): $(MANAGER_SRC)
	$(CC) $(CFLAGS) -o $@ $<

$(CLIENT): $(CLIENT_SRC)
	$(CC) $(CFLAGS) -o $@ $<

$(CONSOLE): $(CONSOLE_SRC)
	$(CC) $(CFLAGS) -o $@ $<

# === CLEAN TARGET ===
.PHONY: clean
clean:
	@rm -f $(MANAGER) $(CLIENT) $(CONSOLE) $(MANAGER_LOG)
	@rm -f $(CLIENT2_DIR)/*.txt

# === HELP TARGET ===
.PHONY: help
help:
	@echo ""
	@echo "Available targets:"
	@echo "  make           - Compile nfs_manager and nfs_client"
	@echo "  make clean     - Remove binaries and logs"
	@echo "  make help      - Show this help message"
	@echo ""

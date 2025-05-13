# Makefile for nfs sync system

# === CONFIG ===
SRC_DIR := src
CONFIG_DIR := config
CONFIG_FILE := $(CONFIG_DIR)/config.txt

CLIENT1_DIR := client1_dir
CLIENT2_DIR := client2_dir

MANAGER := nfs_manager
CLIENT := nfs_client

MANAGER_SRC := $(SRC_DIR)/nfs_manager.c
CLIENT_SRC := $(SRC_DIR)/nfs_client.c

MANAGER_LOG := manager.log

CC := gcc
CFLAGS := -Wall -Wextra -pthread

# === DEFAULT TARGET ===
.PHONY: all
all: $(MANAGER) $(CLIENT)

# === BUILD TARGETS ===
$(MANAGER): $(MANAGER_SRC)
	@echo "Compiling $(MANAGER)..."
	$(CC) $(CFLAGS) -o $@ $<

$(CLIENT): $(CLIENT_SRC)
	@echo "Compiling $(CLIENT)..."
	$(CC) $(CFLAGS) -o $@ $<

# === RUN TARGET ===
.PHONY: run
run: all
	@echo "Starting nfs clients and manager..."
	@mkdir -p $(CLIENT1_DIR) $(CLIENT2_DIR)
	@echo "file1.txt content" > $(CLIENT1_DIR)/file1.txt
	@echo "file2.txt content" > $(CLIENT1_DIR)/file2.txt
	@cd $(CLIENT1_DIR) && ../$(CLIENT) -p 8001 &
	@sleep 1
	@cd $(CLIENT2_DIR) && ../$(CLIENT) -p 8002 &
	@sleep 1
	@./$(MANAGER) -l $(MANAGER_LOG) -c $(CONFIG_FILE) -n 2 -p 9000 -b 10

# === CLEAN TARGET ===
.PHONY: clean
clean:
	@echo "Cleaning binaries and logs..."
	@rm -f $(MANAGER) $(CLIENT) $(MANAGER_LOG)
	@rm -f $(CLIENT1_DIR)/*.txt $(CLIENT2_DIR)/*.txt

# === HELP TARGET ===
.PHONY: help
help:
	@echo ""
	@echo "Available targets:"
	@echo "  make           - Compile nfs_manager and nfs_client"
	@echo "  make run       - Start two clients and manager (with default setup)"
	@echo "  make clean     - Remove binaries and logs"
	@echo "  make help      - Show this help message"
	@echo ""

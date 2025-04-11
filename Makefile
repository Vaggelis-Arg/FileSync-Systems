CC = gcc
CFLAGS = -Wall -Werror -std=gnu11
BIN_DIR = bin
SRC_DIR = src
SCRIPTS_DIR = scripts

all: directories fss_manager fss_console worker fss_script

directories:
	mkdir -p $(BIN_DIR) logs pipes

fss_manager: $(SRC_DIR)/fss_manager.c $(SRC_DIR)/utils.h
	$(CC) $(CFLAGS) -o $(BIN_DIR)/fss_manager $(SRC_DIR)/fss_manager.c -lrt

fss_console: $(SRC_DIR)/fss_console.c $(SRC_DIR)/utils.h
	$(CC) $(CFLAGS) -o $(BIN_DIR)/fss_console $(SRC_DIR)/fss_console.c

worker: $(SRC_DIR)/worker.c $(SRC_DIR)/utils.h
	$(CC) $(CFLAGS) -o $(BIN_DIR)/worker $(SRC_DIR)/worker.c

fss_script:
	cp $(SCRIPTS_DIR)/fss_script.sh $(BIN_DIR)/fss_script.sh
	chmod +x $(BIN_DIR)/fss_script.sh

clean:
	rm -rf $(BIN_DIR) logs pipes
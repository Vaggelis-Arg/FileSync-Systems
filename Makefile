CC = gcc
CFLAGS = -Wall -Werror -O2 -g
LDFLAGS = 

SRC_DIR = src
OBJ_DIR = build

CONSOLE_SRC = $(SRC_DIR)/fss_console.c
MANAGER_SRC = $(SRC_DIR)/fss_manager.c
WORKER_SRC = $(SRC_DIR)/worker.c

CONSOLE_OBJ = $(OBJ_DIR)/fss_console.o
MANAGER_OBJ = $(OBJ_DIR)/fss_manager.o
WORKER_OBJ = $(OBJ_DIR)/worker.o

BINARIES = fss_console fss_manager worker

.PHONY: all clean

all: $(BINARIES)

# Create build directory if it doesn't exist
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

fss_console: $(CONSOLE_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

fss_manager: $(MANAGER_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

worker: $(WORKER_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -rf $(OBJ_DIR) *.o $(BINARIES)

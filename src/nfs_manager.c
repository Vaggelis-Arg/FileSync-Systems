/* File: nfs_manager.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

typedef struct sync_info SyncInfo;

struct sync_info {
    char source_host[64];
    int source_port;
    char source_dir[128];
    char target_host[64];
    int target_port;
    char target_dir[128];
    int active;
    time_t last_sync;
    int error_count;
    SyncInfo *next;
};

SyncInfo *sync_info_mem_store = NULL;

char manager_logfile[128];
char config_file[128];
int worker_limit = 5;
int port_number = 0;
int buffer_size = 0;

int main(int argc, char *argv[]) {
	if (argc < 9) {
        fprintf(stderr, "Usage: %s -l <logfile> -c <config_file> [-n <worker_limit>] -p <port_number> -b <bufferSize>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

	for (int i = 1; i < argc; i += 2) {
        if (!strcmp(argv[i], "-l") && i + 1 < argc) {
            strncpy(manager_logfile, argv[i + 1], sizeof(manager_logfile));
        }
		else if (!strcmp(argv[i], "-c") && i + 1 < argc) {
            strncpy(config_file, argv[i + 1], sizeof(config_file));
        }
		else if (!strcmp(argv[i], "-n") && i + 1 < argc) {
            worker_limit = atoi(argv[i + 1]);
			if(worker_limit <= 0) {
				fprintf(stderr, "Worker limit should be a positive integer\n");
				exit(EXIT_FAILURE);
			}
        }
		else if (!strcmp(argv[i], "-p") && i + 1 < argc) {
            port_number = atoi(argv[i + 1]);
			if(port_number <= 0) {
				fprintf(stderr, "Port number should be a positive integer\n");
				exit(EXIT_FAILURE);
			}
        }
		else if (!strcmp(argv[i], "-b") && i + 1 < argc) {
            buffer_size = atoi(argv[i + 1]);
			if(buffer_size <= 0) {
				fprintf(stderr, "Buffer size should be a positive integer\n");
				exit(EXIT_FAILURE);
			}
        }
		else {
            fprintf(stderr, "Unknown or incomplete option %s\n", argv[i]);
            exit(EXIT_FAILURE);
        }
    }

    printf("manager_logfile: %s\n", manager_logfile);
    printf("config_file: %s\n", config_file);
    printf("worker_limit: %d\n", worker_limit);
    printf("port_number: %d\n", port_number);
    printf("buffer_size: %d\n", buffer_size);

	return 0;
}
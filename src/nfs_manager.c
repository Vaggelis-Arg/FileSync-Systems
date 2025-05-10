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
	
	return 0;
}
/* File: nfs_manager.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netdb.h>


typedef struct sync_info SyncInfo;

typedef struct sync_queue_task SyncTask;

struct sync_info {
    char source_dir[100];
	char target_dir[100];
	char source_host[50];
    char target_host[50];
    int target_port;
	int source_port;
    int active;
	int error_count;
    time_t last_sync;
    SyncInfo *next;
};

struct sync_queue_task {
    char filename[50];
    char source_dir[100];
	char target_dir[100];
	char source_host[50];
	char target_host[50];
    int source_port;
    int target_port;
};

static SyncInfo *sync_info_mem_store = NULL;

static SyncTask *task_buffer = NULL;
static int buffer_head = 0;
static int buffer_tail = 0;
static int buffer_count = 0;

static pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t not_empty = PTHREAD_COND_INITIALIZER;
static pthread_cond_t not_full = PTHREAD_COND_INITIALIZER;

static char manager_logfile[40];
static int worker_limit = 5;
static int port_number = 0;
static int buffer_size = 0;

static void log_sync_result(SyncTask task, const char *operation, const char *result, const char *details) {
    FILE *fp = fopen(manager_logfile, "a");
    if (fp == NULL) return;
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    fprintf(fp, "[%04d-%02d-%02d %02d:%02d:%02d] [%s/%s@%s:%d] [%s/%s@%s:%d] [%ld] [%s] [%s] [%s]\n",
            t->tm_year + 1900,t->tm_mon + 1, t->tm_mday,t->tm_hour, t->tm_min, t->tm_sec,task.source_dir, task.filename, task.source_host,  task.source_port,
            task.target_dir, task.filename,task.target_host, task.target_port,pthread_self(), operation, result, details);
    fclose(fp);
}



void enqueue_task(SyncTask task) {
	pthread_mutex_lock(&buffer_mutex); // lock the buffer mutex (no other thread can operate in buffer until current thread finishes)
	while(buffer_count >= buffer_size) {
		pthread_cond_wait(&not_full, &buffer_mutex);
	}
	task_buffer[buffer_tail] = task;
	buffer_tail = (buffer_tail + 1) % buffer_size;
	buffer_count++;
	pthread_cond_signal(&not_empty); // We added the task, so queue is not empty (signal for any thread which tries to dequeue a task)
	pthread_mutex_unlock(&buffer_size); // I finished, next thread can now operate in the task queue
}

SyncTask dequeue_task() {
	pthread_mutex_lock(&buffer_mutex);
	while(buffer_count <= 0) {
		pthread_cond_wait(&not_empty, &buffer_mutex);
	}
	SyncTask task = task_buffer[buffer_head];
	buffer_head = (buffer_head + 1) % buffer_size;
	buffer_count--;

	pthread_cond_signal(&not_full); // I just removed a task from the queue so it is definitely not full. If it was and a thread could not enqueue a task, now it can
	pthread_mutex_unlock(&buffer_mutex);
	return task;
}

static void parse_config_file(char *config) {
	FILE *fp = fopen(config, "r");
    if (!fp) {
		fprintf(stderr, "Error opening config file %s\n", config);
        exit(EXIT_FAILURE);
    }

	char line[200];
	while (fgets(line, sizeof(line), fp)) {
		char src[100], target[100];

		if (sscanf(line, "%s %s", src, target) != 2) {
            fprintf(stderr, "Incorect config file\n");
            exit(EXIT_FAILURE);
        }

		char src_path[50], src_host[50], target_path[50], target_host[50];
        int src_port, target_port;

		char *src_path_end = strchr(src, '@');
    	if (!src_path_end) {
			fprintf(stderr, "Incorect config file\n");
			exit(EXIT_FAILURE);
		}
		*src_path_end = '\0';
		strncpy(src_path, src, 50);

		char *src_host_end = strchr(src_path_end + 1, ':');
		if (!src_host_end) {
			fprintf(stderr, "Incorect config file\n");
			exit(EXIT_FAILURE);
		}
		*src_host_end = '\0';
		strncpy(src_host, src_path_end + 1, 50);

		src_port = atoi(src_host_end + 1);

		char *target_path_end = strchr(target, '@');
    	if (!target_path_end) {
			fprintf(stderr, "Incorect config file\n");
			exit(EXIT_FAILURE);
		}
		*target_path_end = '\0';
		strncpy(target_path, target, 50);

		char *target_host_end = strchr(target_path_end + 1, ':');
		if (!target_host_end) {
			fprintf(stderr, "Incorect config file\n");
			exit(EXIT_FAILURE);
		}
		*target_host_end = '\0';
		strncpy(target_host, target_path_end + 1, 50);

		target_port = atoi(target_host_end + 1);

		SyncInfo *node = malloc(sizeof(*node)); // Create new node in sync_info_mem_store to store the config
        if (node == NULL) {
            fprintf(stderr, "Error in memory allocation\n");
            exit(EXIT_FAILURE);
        }

		strncpy(node->source_dir, src_path, sizeof(node->source_dir));
        strncpy(node->source_host, src_host, sizeof(node->source_host));
        node->source_port = src_port;
        strncpy(node->target_dir, target_path, sizeof(node->target_dir));
        strncpy(node->target_host, target_host, sizeof(node->target_host));
        node->target_port = target_port;

        node->active = 1;
        node->last_sync = 0;
        node->error_count = 0;
        node->next = sync_info_mem_store;
        sync_info_mem_store = node;
	}
}

void *worker_thread(void) {};

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
			char config_file[40];
            strncpy(config_file, argv[i + 1], sizeof(config_file));
			parse_config_file(config_file);
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

	task_buffer = malloc(sizeof(SyncTask) * buffer_size);
	if (!task_buffer) {
		fprintf(stderr, "Error in memory allocation\n");
		exit(EXIT_FAILURE);
	}

	pthread_t *worker_threads = malloc(worker_limit * sizeof(pthread_t));
	if(worker_threads == NULL) {
		fprintf(stderr, "Error in memory allocation\n");
		exit(EXIT_FAILURE);
	}

	for(int i = 0 ; i < worker_limit ; i++) {
		int error = pthread_create(&worker_threads[i], NULL, worker_thread, NULL);
		if(error) {
			fprintf(stderr, "Error in thread creation\n");
			exit(EXIT_FAILURE);
		}
	}

	return 0;
}
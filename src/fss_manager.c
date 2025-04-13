/* File: fss_manager.c */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/inotify.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>

typedef struct SyncInfo {
    char *source;
    char *target;
	int wd;
    time_t last_sync;
    int active;
    int error_count;
    struct SyncInfo *next;
} SyncInfo;

SyncInfo *config = NULL;

int worker_limit = 5;

typedef struct WorkerTask {
    char *source;
    char *target;
	char *filename;
    char *operation;
    struct WorkerTask *next;
} WorkerTask;

WorkerTask *task_queue = NULL;
int active_workers = 0;

int inotify_fd;

void create_named_pipes() {
    // Remove existing pipes
    unlink("fss_in");
    unlink("fss_out");
    
    // Create new pipes
    if (mkfifo("fss_in", 0666) == -1) {
        perror("mkfifo fss_in failed");
        exit(EXIT_FAILURE);
    }
    if (mkfifo("fss_out", 0666) == -1) {
        perror("mkfifo fss_out failed");
        exit(EXIT_FAILURE);
    }
    printf("Named pipes created\n");
}

void clean_logs(const char *logfile) {
    // Truncate existing log file (or remove and recreate)
    FILE *fp = fopen(logfile, "w");
    if (fp) {
        fclose(fp);
        printf("Cleaned log file: %s\n", logfile);
    } else {
        perror("Failed to clean log file");
    }
}

SyncInfo* parse_config(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) return NULL;

    SyncInfo *head = NULL;
    char line[256];
    
    while (fgets(line, sizeof(line), fp)) {
        char *source = strtok(line, " ");
        char *target = strtok(NULL, " \n");
        
        SyncInfo *new_node = malloc(sizeof(SyncInfo));
        new_node->source = strdup(source);
        new_node->target = strdup(target);
        new_node->next = head;
        head = new_node;
    }
    fclose(fp);
    return head;
}

void start_worker_with_operation(const char *source, const char *target, 
	const char *filename, const char *operation) {
    if (active_workers >= worker_limit) {
        // Add to queue
        WorkerTask *new_task = malloc(sizeof(WorkerTask));
        new_task->source = strdup(source);
        new_task->target = strdup(target);
		new_task->filename = strdup(filename);
		new_task->operation = strdup(operation);
        new_task->next = task_queue;
        task_queue = new_task;
        printf("Worker queue full. Queued operation: %s on %s\n", operation, source);
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        // Worker process
        execl("./worker", "worker", source, target, filename, operation, NULL);
        perror("execl");
        exit(EXIT_FAILURE);
    } else if (pid > 0) {
        active_workers++;
        printf("Started worker PID: %d for %s (%s)\n", pid, operation, filename);
    } else {
        perror("fork");
    }
}

void sigchld_handler(int sig) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        active_workers--;
        printf("Worker %d exited. Active: %d/%d\n", 
              pid, active_workers, worker_limit);
        
        // Process queued tasks
        while (task_queue && active_workers < worker_limit) {
            WorkerTask *task = task_queue;
            task_queue = task_queue->next;
            
            start_worker_with_operation(
                task->source,
                task->target,
                task->filename,
                task->operation
            );
            
            free(task->source);
            free(task->target);
            free(task->filename);
            free(task->operation);
            free(task);
        }
    }
}

void setup_inotify() {
    inotify_fd = inotify_init();
    if (inotify_fd == -1) {
        perror("inotify_init");
        exit(EXIT_FAILURE);
    }
}

SyncInfo* find_sync_info_by_wd(int wd) {
    SyncInfo *current = config;
    while (current) {
        if (current->wd == wd) {
            return current;
        }
        current = current->next;
    }
    return NULL; // Not found
}

void handle_inotify_events() {
    char buffer[4096];
    ssize_t len = read(inotify_fd, buffer, sizeof(buffer));
    
    for (char *ptr = buffer; ptr < buffer + len; ) {
        struct inotify_event *event = (struct inotify_event *)ptr;
        SyncInfo *info = find_sync_info_by_wd(event->wd);
        
        if (info && event->len > 0) {
            char *filename = event->name;
            char operation[10];
            
            if (event->mask & IN_CREATE) strcpy(operation, "ADDED");
            else if (event->mask & IN_MODIFY) strcpy(operation, "MODIFIED");
            else if (event->mask & IN_DELETE) strcpy(operation, "DELETED");
            
            // Start worker with specific operation
            start_worker_with_operation(info->source, info->target, filename, operation);
        }
        ptr += sizeof(struct inotify_event) + event->len;
    }
}

void log_message(const char *logfile, const char *message) {
    FILE *fp = fopen(logfile, "a");
    if (!fp) return;
    
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    fprintf(fp, "[%04d-%02d-%02d %02d:%02d:%02d] %s\n",
            t->tm_year+1900, t->tm_mon+1, t->tm_mday,
            t->tm_hour, t->tm_min, t->tm_sec, message);
    fclose(fp);
}

void process_command(const char *command, const char *logfile) {
    char cmd[32], source[256], target[256];
    if (sscanf(command, "%s %s %s", cmd, source, target) < 2) {
        return;
    }

    if (strcmp(cmd, "add") == 0) {
        // Check if already exists
        SyncInfo *current = config;
        while (current) {
            if (strcmp(current->source, source) == 0) {
                printf("Already in queue: %s\n", source);
                return;
            }
            current = current->next;
        }

        // Add new sync info
        SyncInfo *new_node = malloc(sizeof(SyncInfo));
        new_node->source = strdup(source);
        new_node->target = strdup(target);
        new_node->active = 1;
        new_node->next = config;
        config = new_node;

        // Log and start worker
        char log_msg[1024];
        snprintf(log_msg, sizeof(log_msg), "Added directory: %s -> %s", source, target);
        log_message(logfile, log_msg);

		// Add inotify watch
        new_node->wd = inotify_add_watch(inotify_fd, new_node->source, 
			IN_CREATE | IN_MODIFY | IN_DELETE);
		if (new_node->wd == -1) {
			printf("Failed to watch %s\n", new_node->source);
			snprintf(log_msg, sizeof(log_msg), "Failed to watch %s", new_node->source);
        	log_message(logfile, log_msg);
		} else {
			printf("Monitoring started for %s (wd=%d)\n", new_node->source, new_node->wd);
			snprintf(log_msg, sizeof(log_msg), "Monitoring started for %s (wd=%d)", new_node->source, new_node->wd);
        	log_message(logfile, log_msg);
		}
        
        start_worker_with_operation(source, target, "ALL", "FULL");
    }
    else if (strcmp(cmd, "cancel") == 0) {
        SyncInfo *current = config;
        while (current) {
            if (strcmp(current->source, source) == 0) {
                current->active = 0;
                inotify_rm_watch(inotify_fd, current->wd);
                
                char log_msg[512];
                printf("Monitoring stopped for %s\n", source);
				snprintf(log_msg, sizeof(log_msg), "Monitoring stopped for %s", source);
                log_message(logfile, log_msg);
                return;
            }
            current = current->next;
        }
        printf("Directory not monitored: %s\n", source);
    }
    else if (strcmp(cmd, "sync") == 0) {
        SyncInfo *current = config;
        while (current) {
            if (strcmp(current->source, source) == 0 && current->active) {
                char log_msg[512];
                snprintf(log_msg, sizeof(log_msg), "Syncing directory: %s -> %s", source, current->target);
                log_message(logfile, log_msg);
                
                start_worker_with_operation(source, current->target, "ALL", "FULL");
                return;
            }
            current = current->next;
        }
        printf("Directory not monitored: %s\n", source);
    }
    else if (strcmp(cmd, "shutdown") == 0) {
        printf("Shutting down manager...\n");
        exit(EXIT_SUCCESS);
    }
}

int main(int argc, char *argv[]) {
    char *logfile = "manager.log";
    char *config_file = NULL;
    int i = 1;  // Start from first argument after program name

	if(argc < 5) {
		fprintf(stderr, "Usage: %s -l <logfile> -c <config> [-n <workers>]\n", argv[0]);
        exit(EXIT_FAILURE);
	}

    while (i < argc) {
        if (strcmp(argv[i], "-l") == 0) {
            if (i+1 < argc) {
                logfile = argv[i+1];
                i += 2;
            } else {
                fprintf(stderr, "Missing filename for -l option\n");
                exit(EXIT_FAILURE);
            }
        } else if (strcmp(argv[i], "-c") == 0) {
            if (i+1 < argc) {
                config_file = argv[i+1];
                i += 2;
            } else {
                fprintf(stderr, "Missing filename for -c option\n");
                exit(EXIT_FAILURE);
            }
        } else if (strcmp(argv[i], "-n") == 0) {
            if (i+1 < argc) {
                worker_limit = atoi(argv[i+1]);
                i += 2;
            }
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            fprintf(stderr, "Usage: %s -l <logfile> -c <config> [-n <workers>]\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    create_named_pipes();
	clean_logs(logfile);
    SyncInfo *config = parse_config(config_file);
    setup_inotify();
    signal(SIGCHLD, sigchld_handler);

    // Start initial workers with logging
    SyncInfo *current = config;
    while (current) {
        char log_buffer[512];
        snprintf(log_buffer, sizeof(log_buffer), "Added directory: %s -> %s", 
                current->source, current->target);
        log_message(logfile, log_buffer);

		current->wd = inotify_add_watch(inotify_fd, current->source, 
			IN_CREATE | IN_MODIFY | IN_DELETE);
		if (current->wd == -1) {
			printf("Failed to watch %s\n", current->source);
			snprintf(log_buffer, sizeof(log_buffer), "Failed to watch %s", current->source);
        	log_message(logfile, log_buffer);
		} else {
			printf("Monitoring started for %s (wd=%d)\n", current->source, current->wd);
			snprintf(log_buffer, sizeof(log_buffer), "Monitoring started for %s (wd=%d)", current->source, current->wd);
        	log_message(logfile, log_buffer);
		}
        
        start_worker_with_operation(
			current->source, 
			current->target,
			"ALL",
			"FULL"
		);
        current = current->next;
    }

    int fss_in_fd = open("fss_in", O_RDONLY | O_NONBLOCK);
	int fss_out_fd = open("fss_out", O_WRONLY);

	fd_set read_fds;
	while (1) {
		FD_ZERO(&read_fds);
		FD_SET(fss_in_fd, &read_fds);
		FD_SET(inotify_fd, &read_fds);

		int max_fd = (fss_in_fd > inotify_fd) ? fss_in_fd : inotify_fd;
		select(max_fd + 1, &read_fds, NULL, NULL, NULL);

		if (FD_ISSET(inotify_fd, &read_fds)) {
			handle_inotify_events();
		}

		if (FD_ISSET(fss_in_fd, &read_fds)) {
			char command[256];
			ssize_t bytes = read(fss_in_fd, command, sizeof(command));
			if (bytes > 0) {
				process_command(command, logfile);
				// Write response to fss_out
				dprintf(fss_out_fd, "Processed: %s", command);
			}
		}
	}
}
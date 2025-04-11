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
    time_t last_sync;
    int active;
    int error_count;
    struct SyncInfo *next;
} SyncInfo;

int worker_limit = 5;

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

void setup_inotify(SyncInfo *config) {
    int inotify_fd = inotify_init();
    if (inotify_fd == -1) {
        perror("inotify_init");
        exit(EXIT_FAILURE);
    }

    SyncInfo *current = config;
    while (current) {
        int wd = inotify_add_watch(inotify_fd, current->source, 
                                  IN_CREATE | IN_MODIFY | IN_DELETE);
        if (wd == -1) {
            printf("Failed to watch %s\n", current->source);
        } else {
            printf("Watching: %s (wd=%d)\n", current->source, wd);
        }
        current = current->next;
    }
}

void start_worker(const char *source, const char *target) {
    pid_t pid = fork();
    if (pid == 0) {
        execl("./worker", "worker", source, target, NULL);
        perror("execl");
        exit(EXIT_FAILURE);
    } else if (pid > 0) {
        printf("Started worker PID: %d\n", pid);
    } else {
        perror("fork");
    }
}

void sigchld_handler(int sig) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        printf("Worker %d exited\n", pid);
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
    SyncInfo *config = parse_config(config_file);
    setup_inotify(config);
    signal(SIGCHLD, sigchld_handler);

    // Start initial workers with logging
    SyncInfo *current = config;
    while (current) {
        char log_buffer[512];
        snprintf(log_buffer, sizeof(log_buffer), "Added directory: %s -> %s", 
                current->source, current->target);
        log_message(logfile, log_buffer);
        
        start_worker(current->source, current->target);
        current = current->next;
    }

    // Main loop would go here
    while (1) pause();  // Temporary placeholder
}
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
#include <sys/select.h>
#include <errno.h>

typedef struct SyncInfo {
    char *source;
    char *target;
	int wd;
    time_t last_sync;
    int active;
    int error_count;
	pid_t last_worker_pid;
	int worker_pipe_fd;
    char *last_operation;
    char *last_worker_details;
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
        // Skip empty lines or lines with only whitespace
        if (strspn(line, " \t\n") == strlen(line)) continue;

        char *source = strtok(line, " \t\n");
        char *target = strtok(NULL, " \t\n");
        
        // Skip if source is missing or target is missing
        if (!source || !target) {
            fprintf(stderr, "Invalid config line: %s", line);
            continue;
        }

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
        // Add to queue (same as before)
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

    // Create pipe for worker communication
    int worker_pipe[2];
    if (pipe(worker_pipe) == -1) {
        perror("pipe");
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        // Worker process
        close(worker_pipe[0]); // Close read end
        
        // Redirect stdout to pipe
        dup2(worker_pipe[1], STDOUT_FILENO);
        close(worker_pipe[1]);
        
        execl("./worker", "worker", source, target, filename, operation, NULL);
        perror("execl");
        exit(EXIT_FAILURE);
    } else if (pid > 0) {
        close(worker_pipe[1]); // Close write end in parent
        
        active_workers++;
        printf("Started worker PID: %d for %s (%s)\n", pid, operation, filename);
        
        // Track worker in SyncInfo and store pipe fd
        SyncInfo *current = config;
        while (current) {
            if (strcmp(current->source, source) == 0) {
                current->last_worker_pid = pid;
                current->worker_pipe_fd = worker_pipe[0]; // Store pipe fd
                if (current->last_operation) free(current->last_operation);
                current->last_operation = strdup(operation);
                break;
            }
            current = current->next;
        }
    } else {
        perror("fork");
        close(worker_pipe[0]);
        close(worker_pipe[1]);
    }
}

void sigchld_handler(int sig) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        active_workers--;
        printf("Worker %d exited. Active: %d/%d\n", 
              pid, active_workers, worker_limit);

        // Find and update the worker's sync info
        SyncInfo *current = config;
        while (current) {
            if (current->last_worker_pid == pid) {
                // Close and clean up pipe if still open
                if (current->worker_pipe_fd > 0) {
                    close(current->worker_pipe_fd);
                    current->worker_pipe_fd = -1;
                }
                
                // Update last sync time
                current->last_sync = time(NULL);
                
                // Update error count if worker failed
                if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                    current->error_count++;
                }
                break;
            }
            current = current->next;
        }
        
        // Process queued tasks (same as before)
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

void log_sync_result(const char *logfile, const char *source, const char *target,
	pid_t worker_pid, const char *operation, const char *result,
	const char *details) {
	FILE *fp = fopen(logfile, "a");
	if (!fp) return;

	time_t now = time(NULL);
	struct tm *t = localtime(&now);

	fprintf(fp, "[%04d-%02d-%02d %02d:%02d:%02d] [%s] [%s] [%d] [%s] [%s] [%s]\n",
	t->tm_year+1900, t->tm_mon+1, t->tm_mday,
	t->tm_hour, t->tm_min, t->tm_sec,
	source, target, worker_pid, operation, result, details);

	fclose(fp);
}

void display_exec_report(const char *source, const char *target, 
		const char *operation, const char *status, 
		const char *details, const char *errors) {
	time_t now = time(NULL);
	struct tm *t = localtime(&now);
	char timestamp[20];
	strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", t);

	printf("----------------------------------------------------\n");
	printf("EXEC_REPORT_START\n");
	printf("OPERATION: %s\n", operation);
	printf("STATUS: %s\n", status);
	printf("DETAILS: %s\n", details);
	if (errors != NULL && strlen(errors) > 0) {
		printf("ERRORS:\n%s\n", errors);
	}
	printf("EXEC_REPORT_END\n");
	printf("----------------------------------------------------\n");
	fflush(stdout);
}

void process_command(const char *command, const char *logfile, int fss_in_fd, int fss_out_fd) {
    char cmd[32], source[128], target[128];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char timestamp[20];
    char log_msg[512];
    char response[1024];
    
    // Format timestamp for logging
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", t);
    
    if (sscanf(command, "%s %s %s", cmd, source, target) < 1) {
        // Handle invalid command format
        snprintf(response, sizeof(response), 
                "[%s] Invalid command format\n", timestamp);
		ssize_t written = write(fss_out_fd, response, strlen(response));
		if (written == -1) {
			perror("write to fss_out_fd failed");
		}
        return;
    }

    if (strcmp(cmd, "add") == 0) {
        // Check if already exists
        SyncInfo *current = config;
        while (current) {
            if (strcmp(current->source, source) == 0) {
                snprintf(response, sizeof(response),
                        "[%s] Already in queue: %s\n", timestamp, source);
				ssize_t written = write(fss_out_fd, response, strlen(response));
				if (written == -1) {
					perror("write to fss_out_fd failed");
				}
                return;
            }
            current = current->next;
        }

        // Add new sync info
        SyncInfo *new_node = malloc(sizeof(SyncInfo));
        new_node->source = strdup(source);
        new_node->target = strdup(target);
        new_node->active = 1;
        new_node->error_count = 0;
        new_node->last_sync = time(NULL);
        new_node->next = config;
        config = new_node;

        // Log and respond
        snprintf(log_msg, sizeof(log_msg), "Added directory: %s -> %s", source, target);
        log_message(logfile, log_msg);

        // Add inotify watch
        new_node->wd = inotify_add_watch(inotify_fd, new_node->source, 
                IN_CREATE | IN_MODIFY | IN_DELETE);
        if (new_node->wd == -1) {
            snprintf(log_msg, sizeof(log_msg), "Failed to monitor %s", new_node->source);
            log_message(logfile, log_msg);
            
            snprintf(response, sizeof(response),
                    "[%s] Failed to monitor %s\n", timestamp, new_node->source);
			ssize_t written = write(fss_out_fd, response, strlen(response));
			if (written == -1) {
				perror("write to fss_out_fd failed");
			}
        } else {
            snprintf(log_msg, sizeof(log_msg), 
                    "Monitoring started for %s (wd=%d)", new_node->source, new_node->wd);
            log_message(logfile, log_msg);
			snprintf(response, sizeof(response),
                "[%s] Added directory: %s -> %s\n[%s] Monitoring started for %s (wd=%d)\n",
                timestamp, source, target, timestamp, new_node->source, new_node->wd);
			ssize_t written = write(fss_out_fd, response, strlen(response));
			if (written == -1) {
				perror("write to fss_out_fd failed");
			}
    		fsync(fss_out_fd);
        }
        
        start_worker_with_operation(source, target, "ALL", "FULL");
    }
    else if (strcmp(cmd, "status") == 0) {
        // Log the status request
        snprintf(log_msg, sizeof(log_msg), 
                "Status requested for %s", source);
        log_message(logfile, log_msg);

        SyncInfo *current = config;
        int found = 0;
        while (current) {
            if (strcmp(current->source, source) == 0) {
                found = 1;
                char last_sync_time[20];
                struct tm *last_t = localtime(&current->last_sync);
                strftime(last_sync_time, sizeof(last_sync_time), 
                        "%Y-%m-%d %H:%M:%S", last_t);
                
                snprintf(response, sizeof(response),
                        "[%s] Status requested for %s\n"
                        "Directory: %s\n"
                        "Target: %s\n"
                        "Last Sync: %s\n"
                        "Errors: %d\n"
                        "Status: %s\n",
                        timestamp, source,
                        current->source, current->target,
                        last_sync_time,
                        current->error_count,
                        current->active ? "Active" : "Inactive");
				ssize_t written = write(fss_out_fd, response, strlen(response));
				if (written == -1) {
					perror("write to fss_out_fd failed");
				}
    			fsync(fss_out_fd);
                break;
            }
            current = current->next;
        }
        
        if (!found) {
            snprintf(response, sizeof(response), 
                    "[%s] Directory not monitored: %s\n", timestamp, source);
			ssize_t written = write(fss_out_fd, response, strlen(response));
			if (written == -1) {
				perror("write to fss_out_fd failed");
			}
		    fsync(fss_out_fd);
        }
    }
    else if (strcmp(cmd, "cancel") == 0) {
		SyncInfo *current = config;
		SyncInfo *prev = NULL;
		int found = 0;
		
		while (current) {
			if (strcmp(current->source, source) == 0) {
				found = 1;
				if (current->active) {
					current->active = 0;
					inotify_rm_watch(inotify_fd, current->wd);
					
					// Log to both console and log file
					snprintf(log_msg, sizeof(log_msg), 
							"Monitoring stopped for %s", source);
					log_message(logfile, log_msg);
					
					snprintf(response, sizeof(response),
							"[%s] Monitoring stopped for %s\n", 
							timestamp, source);
				} else {
					// Directory exists but already inactive - console only
					snprintf(response, sizeof(response),
							"[%s] Directory not monitored: %s\n", 
							timestamp, source);
				}

				if (prev == NULL) {
					config = current->next;
				} else {
					prev->next = current->next;
				}
				free(current);
				
				ssize_t written = write(fss_out_fd, response, strlen(response));
				if (written == -1) {
					perror("write to fss_out_fd failed");
				}
				fsync(fss_out_fd);
				break;
			}
			prev = current;
			current = current->next;
		}
		
		if (!found) {
			// Directory not found - console only
			snprintf(response, sizeof(response),
					"[%s] Directory not monitored: %s\n", 
					timestamp, source);
			ssize_t written = write(fss_out_fd, response, strlen(response));
			if (written == -1) {
				perror("write to fss_out_fd failed");
			}
			fsync(fss_out_fd);
		}
	}
    else if (strcmp(cmd, "sync") == 0) {
        SyncInfo *current = config;
        while (current) {
            if (strcmp(current->source, source) == 0 && current->active) {
                snprintf(log_msg, sizeof(log_msg), 
                        "Syncing directory: %s -> %s", source, current->target);
                log_message(logfile, log_msg);
                
                snprintf(response, sizeof(response),
                        "[%s] Syncing directory: %s -> %s\n", 
                        timestamp, source, current->target);
				ssize_t written = write(fss_out_fd, response, strlen(response));
				if (written == -1) {
					perror("write to fss_out_fd failed");
				}
			    fsync(fss_out_fd);
                
                start_worker_with_operation(source, current->target, "ALL", "FULL");

				snprintf(response, sizeof(response),
						"[%s] Sync completed %s -> %s Errors:%d\n",
						timestamp, source, current->target, current->error_count);
				written = write(fss_out_fd, response, strlen(response));
				if (written == -1) {
					perror("write to fss_out_fd failed");
				}
				fsync(fss_out_fd);

                return;
            }
            current = current->next;
        }
        
        snprintf(response, sizeof(response),
                "[%s] Directory not monitored: %s\n", timestamp, source);
		ssize_t written = write(fss_out_fd, response, strlen(response));
		if (written == -1) {
			perror("write to fss_out_fd failed");
		}
		fsync(fss_out_fd);
    }
    else if (strcmp(cmd, "shutdown") == 0) {
        snprintf(log_msg, sizeof(log_msg), "Shutting down manager");
        log_message(logfile, log_msg);
        
        snprintf(response, sizeof(response),
                "[%s] Shutting down manager...\n"
                "[%s] Waiting for active workers to finish...\n"
                "[%s] Processing remaining tasks...\n"
                "[%s] Manager shutdown complete\n",
                timestamp, timestamp, timestamp, timestamp);
		ssize_t written = write(fss_out_fd, response, strlen(response));
		if (written == -1) {
			perror("write to fss_out_fd failed");
		}
		fsync(fss_out_fd);
        
        // Wait for active workers
        while (active_workers > 0) {
            sleep(1);
        }
        
        // Process queued tasks
        while (task_queue) {
            WorkerTask *task = task_queue;
            start_worker_with_operation(task->source, task->target, 
                                      task->filename, task->operation);
            task_queue = task_queue->next;
            free(task->source);
            free(task->target);
            free(task->filename);
            free(task->operation);
            free(task);
        }
        
        exit(EXIT_SUCCESS);
    }
    else {
        snprintf(response, sizeof(response),
                "[%s] Unknown command: %s\n", timestamp, cmd);
		ssize_t written = write(fss_out_fd, response, strlen(response));
		if (written == -1) {
			perror("write to fss_out_fd failed");
		}
		fsync(fss_out_fd);
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
    config = parse_config(config_file);
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
			printf("Failed to monitor %s\n", current->source);
			snprintf(log_buffer, sizeof(log_buffer), "Failed to monitor %s", current->source);
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

    int fss_in_fd = open("fss_in", O_RDONLY);
    int fss_out_fd = open("fss_out", O_WRONLY);

    if (fss_in_fd == -1 || fss_out_fd == -1) {
        perror("Failed to open pipes");
        exit(EXIT_FAILURE);
    }

    fd_set read_fds;
    while (1) {
        FD_ZERO(&read_fds);
		FD_SET(fss_in_fd, &read_fds);
		FD_SET(inotify_fd, &read_fds);
		
		// Add all active worker pipes to the select set
		int max_fd = fss_in_fd > inotify_fd ? fss_in_fd : inotify_fd;
		SyncInfo *current = config;
		while (current) {
			if (current->worker_pipe_fd > 0) {
				FD_SET(current->worker_pipe_fd, &read_fds);
				if (current->worker_pipe_fd > max_fd) {
					max_fd = current->worker_pipe_fd;
				}
			}
			current = current->next;
		}

		if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) == -1) {
			if (errno == EINTR) {
				continue;
			}
			perror("select");
			continue;
		}

		// Check for worker output first
		current = config;
		while (current) {
			if (current->worker_pipe_fd > 0 && FD_ISSET(current->worker_pipe_fd, &read_fds)) {
				char report[1024];
				ssize_t bytes = read(current->worker_pipe_fd, report, sizeof(report) - 1);
				
				if (bytes > 0) {
					report[bytes] = '\0';
					char *worker_tag = strstr(report, "[WORKER_REPORT]");
					if (worker_tag) {
						// Parse worker report
						char timestamp[32], source_dir[256], target_dir[256];
						int worker_pid;
						char operation[20], status[20], details[256];
						
						if (sscanf(report, "[%31[^]]] [%*[^]]] [%255[^]]] [%255[^]]] [%d] [%19[^]]] [%19[^]]] [%255[^]]]",
								timestamp, source_dir, target_dir, &worker_pid, 
								operation, status, details) >= 6) {
							
							// Log the result
							log_sync_result(logfile, source_dir, target_dir, 
										worker_pid, operation, status, details);
							
							// Update sync info with details
							SyncInfo *info = config;
							while (info) {
								if (info->last_worker_pid == worker_pid) {
									if (info->last_worker_details) 
										free(info->last_worker_details);
									info->last_worker_details = strdup(details);
									break;
								}
								info = info->next;
							}
						}
						if (strcmp(status, "ERROR") == 0) {
                            display_exec_report(source_dir, target_dir, operation, 
                                              status, "", details);
                        } else {
                            display_exec_report(source_dir, target_dir, operation, 
                                              status, details, "");
                        }
					}
				} else if (bytes == 0) {
					// Pipe was closed - worker finished
					close(current->worker_pipe_fd);
					current->worker_pipe_fd = -1;
				} else if (bytes == -1) {
					perror("read from worker pipe");
					close(current->worker_pipe_fd);
					current->worker_pipe_fd = -1;
				}
			}
			current = current->next;
		}

        if (FD_ISSET(inotify_fd, &read_fds)) {
            handle_inotify_events();
        }

        if (FD_ISSET(fss_in_fd, &read_fds)) {
            char command[256];
            ssize_t bytes = read(fss_in_fd, command, sizeof(command) - 1);
            if (bytes > 0) {
                command[bytes] = '\0';
                // Remove trailing newline if present
                if (command[strlen(command)-1] == '\n') {
                    command[strlen(command)-1] = '\0';
                }
                process_command(command, logfile, fss_in_fd, fss_out_fd);
            } else if (bytes == 0) {
                // Pipe was closed - reopen it
                close(fss_in_fd);
                fss_in_fd = open("fss_in", O_RDONLY);
                if (fss_in_fd == -1) {
                    perror("Failed to reopen fss_in");
                }
            }
        }
	}
}
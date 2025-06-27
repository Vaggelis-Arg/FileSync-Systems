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
#include <sys/time.h>

typedef struct sync_info SyncInfo;

typedef struct worker_queue_item WorkerQueueItem;

struct sync_info {
	char *source;
	char *target;
	int wd;
	int active;
	time_t last_sync;
	pid_t last_worker_pid;
	unsigned int error_count;
	int worker_pipe_fd;
	char *last_operation;
	SyncInfo *next;
};

struct worker_queue_item {
	char *source;
	char *target;
	char *filename;
	char *operation;
	WorkerQueueItem *next;
};

static SyncInfo *sync_info_mem_store = NULL; // Linked list of sync tasks, each representing a source directory being monitored or processed
WorkerQueueItem *task_queue = NULL; // Queue for tasks that cannot be processed right now

static int worker_limit = 5;
unsigned int active_workers = 0;
int inotify_fd;

static char *logfile;

void log_sync_result(const char *logfile, const char *source, const char *target,
					 pid_t worker_pid, const char *operation, const char *result,
					 const char *details);

void display_exec_report(const char *source, const char *target,
						 const char *operation, const char *status,
						 const char *details, const char *errors);

// Create named pipes fss_in and fss_out
void create_named_pipes() {
	// Delete existing pipes
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
}

// Function to clean manager log file from previous execution
void clean_logs(const char *logfile) {
	FILE *fp = fopen(logfile, "w");
	if (!fp)
		return;
	fclose(fp); // close the file
}

// Function to parse the config data
void parse_config(const char *filename) {
	FILE *fp = fopen(filename, "r");
	if (!fp)
		return;

	char line[100];

	while (fgets(line, sizeof(line), fp)) {
		if (line[0] == '\n' || strspn(line, " \n") == strlen(line))
			continue;

		char *source = strtok(line, " \n");
		char *target = strtok(NULL, " \n");

		if (!source || !target) {
			// skip line if either source or target directories are missing
			fprintf(stderr, "Invalid config line: %s", line);
			continue;
		}

		SyncInfo *new_node = malloc(sizeof(*new_node));
        new_node->source = strdup(source);
        new_node->target = strdup(target);
        new_node->wd = -1;
        new_node->active = 1;
        new_node->last_sync = 0;
        new_node->last_worker_pid = -1;
        new_node->error_count = 0;
        new_node->worker_pipe_fd = -1;
        new_node->last_operation = NULL;
        new_node->next = sync_info_mem_store;
        sync_info_mem_store = new_node;
	}
	fclose(fp);
}

// Start worker for a specific operation in sync info task
void start_worker_with_operation(const char *source, const char *target, const char *filename, const char *operation) {
	if (active_workers >= worker_limit) {
		// If number of active workers exceeds the limit, add the sync task in the queue
		WorkerQueueItem *new_task = malloc(sizeof(*new_task));
		new_task->source = strdup(source);
		new_task->target = strdup(target);
		new_task->filename = strdup(filename);
		new_task->operation = strdup(operation);
		new_task->next = task_queue;
		task_queue = new_task;
		printf("Worker queue full. Queued operation: %s on %s\n", operation, source);
		return;
	}

	// Create pipe for worker/manager communication
	int worker_pipe[2];
	if (pipe(worker_pipe) == -1) {
		perror("pipe");
		return;
	}

	pid_t pid = fork();
	if (pid == 0) {
		// Child/Worker process

		close(worker_pipe[0]); // Close read end

		// Redirect stdout to pipe
		dup2(worker_pipe[1], STDOUT_FILENO);
		close(worker_pipe[1]);

		// Execute worker in the clone that fork created
		execl("./worker", "worker", source, target, filename, operation, NULL);
		perror("execl");
		exit(EXIT_FAILURE);
	}
	else if (pid > 0) {
		// Parent/Manager process

		close(worker_pipe[1]); // Close write end

		active_workers++;
		printf("Started worker PID: %d for %s (%s)\n", pid, operation, filename);

		SyncInfo *curr = sync_info_mem_store;
		while (curr) {
			// Search for the worker to store the pipe file descriptor
			if (strcmp(curr->source, source) == 0) {
				curr->last_worker_pid = pid;
				curr->worker_pipe_fd = worker_pipe[0];
				if (curr->last_operation)
					free(curr->last_operation);
				curr->last_operation = strdup(operation);
				break;
			}
			curr = curr->next;
		}
	}
	else {
		perror("fork");
		close(worker_pipe[0]);
		close(worker_pipe[1]);
	}
}

// Read the report which was printed in the stdout of the worker
void process_worker_report(const char *report, pid_t worker_pid) {
	char *worker_tag = strstr(report, "[WORKER_REPORT]");
	if (worker_tag) {
		char timestamp[50], source_dir[50], target_dir[50];
		char operation[20], status[20], details[300];

		if (sscanf(report, "[%[^]]] [%*[^]]] [%[^]]] [%[^]]] [%*d] [%[^]]] [%[^]]] [%[^]]]",
			timestamp, source_dir, target_dir, operation, status, details) >= 5) {

			log_sync_result(logfile, source_dir, target_dir,
							worker_pid, operation, status, details);

			!strcmp(status, "ERROR")
			? display_exec_report(source_dir, target_dir, operation, status, "", details)
			: display_exec_report(source_dir, target_dir, operation, status, details, "");
		}
	}
}

// Function to handle cleanup and task management when worker processes exit 
void sigchld_handler(int sig) {
	int status;
	pid_t pid;
	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		SyncInfo *curr = sync_info_mem_store;
		while (curr) {
			if (curr->last_worker_pid == pid) {
				// Read remaining data from pipe
				if (curr->worker_pipe_fd > 0) {
					char report[1000];
					ssize_t bytes;
					while ((bytes = read(curr->worker_pipe_fd, report, sizeof(report) - 1)) > 0) {
						report[bytes] = '\0';
						process_worker_report(report, pid);
					}
					close(curr->worker_pipe_fd);
				}

				// Update sync info
				curr->last_sync = time(NULL);
				if (!WIFEXITED(status) || WEXITSTATUS(status))
					curr->error_count++;
				curr->last_worker_pid = -1;
				break;
			}
			curr = curr->next;
		}

		active_workers--;
		printf("Worker %d exited. Active: %d/%d\n", pid, active_workers, worker_limit);

		// If we are able to process tasks in the queue, start workers to sync them
		while (task_queue && active_workers < worker_limit) {
			WorkerQueueItem *task = task_queue;
			task_queue = task_queue->next;

			start_worker_with_operation(task->source, task->target, task->filename, task->operation);

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

SyncInfo *find_sync_info_by_wd(int wd) {
	SyncInfo *curr = sync_info_mem_store;
	while (curr) {
		if (curr->wd == wd) {
			return curr;
		}
		curr = curr->next;
	}
	return NULL;
}

// Function to handle the filesystem events that the program receives from inotify
void handle_inotify_events() {   
	char buffer[4096];
    ssize_t len = read(inotify_fd, buffer, sizeof(buffer));

    if (len <= 0) return;

    for (char *ptr = buffer ; ptr < buffer + len ; ) {
        struct inotify_event *event = (struct inotify_event *)ptr;
		SyncInfo *info = find_sync_info_by_wd(event->wd);

        if (info != NULL && event->len > 0) {
            char *filename = event->name;
            const char *operation = NULL;
			// Check for the operation occured
            if (event->mask & IN_CREATE)
                operation = "ADDED";
            else if (event->mask & IN_MODIFY)
                operation = "MODIFIED";
            else if (event->mask & IN_DELETE)
                operation = "DELETED";

            if (operation != NULL) {
                start_worker_with_operation( info->source, info->target, filename, operation);
            }
        }
		ptr += sizeof(struct inotify_event) + event->len; // move on to the next event in the buffer
    }
}

// Function to log given message in the given logfile
void log_message(const char *logfile, const char *message) {
	FILE *fp = fopen(logfile, "a");
	if (!fp)
		return;

	time_t now = time(NULL);
	struct tm *t = localtime(&now);
	fprintf(fp, "[%04d-%02d-%02d %02d:%02d:%02d] %s\n",
			t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
			t->tm_hour, t->tm_min, t->tm_sec, message);
	fclose(fp);
}

// Function to log result in the format requested in the assignment
void log_sync_result(const char *logfile, const char *source, const char *target, pid_t worker_pid,
					const char *operation, const char *result, const char *details) {
	FILE *fp = fopen(logfile, "a");
	if (!fp)
		return;

	time_t now = time(NULL);
	struct tm *t = localtime(&now);

	fprintf(fp, "[%04d-%02d-%02d %02d:%02d:%02d] [%s] [%s] [%d] [%s] [%s] [%s]\n",
			t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
			t->tm_hour, t->tm_min, t->tm_sec,
			source, target, worker_pid, operation, result, details);

	fclose(fp);
}

// Function to display EXEC_REPORT as it is requested in the assignment
void display_exec_report(const char *source, const char *target, const char *operation, const char *status,
						 const char *details, const char *errors) {
	time_t now = time(NULL);
	struct tm *t = localtime(&now);
	char timestamp[20];
	strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", t);

	printf("EXEC_REPORT_START\n");
	printf("OPERATION: %s\n", operation);
	printf("STATUS: %s\n", status);
	printf("DETAILS: %s\n", details);
	if (errors != NULL && strlen(errors) > 0) {
		printf("ERRORS:\n%s\n", errors);
	}
	printf("EXEC_REPORT_END\n");
	fflush(stdout);
}

// Function to free a sync info node
void free_sync_info(SyncInfo *node) {
    if (node) {
        free(node->source);
        free(node->target);
        if (node->last_operation) {
            free(node->last_operation);
        }
        free(node);
    }
}

// Function to free the entire sync info list
void free_sync_info_list(SyncInfo *head) {
    SyncInfo *curr = head;
    while (curr) {
        SyncInfo *next = curr->next;
        free_sync_info(curr);
        curr = next;
    }
}

// Function to process a command given from the fss_console
void process_command(const char *command, const char *logfile, int fss_in_fd, int fss_out_fd) {
	char cmd[32], source[128], target[128];
	time_t now = time(NULL);
	struct tm *t = localtime(&now);
	char timestamp[20];
	char log_msg[1000];
	char response[1000];

	strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", t);

	if (sscanf(command, "%s %s %s", cmd, source, target) < 1) {
		// Invalid command format
		snprintf(response, sizeof(response),
				 "[%s] Invalid command format\n", timestamp);
		ssize_t written = write(fss_out_fd, response, strlen(response));
		if (written == -1) {
			perror("write to fss_out_fd failed");
		}
		return;
	}

	if (strcmp(cmd, "add") == 0) {
		SyncInfo *curr = sync_info_mem_store;

		// Check if source already exists in sync info list
		while (curr) {
			if (strcmp(curr->source, source) == 0) {
				snprintf(response, sizeof(response),
						 "[%s] Already in queue: %s\n", timestamp, source);
				ssize_t written = write(fss_out_fd, response, strlen(response));
				if (written == -1) {
					perror("write to fss_out_fd failed");
				}
				return;
			}
			curr = curr->next;
		}

		// Add new sync info node in the list
		SyncInfo *new_node = malloc(sizeof(*new_node));
		new_node->source = strdup(source);
		new_node->target = strdup(target);
		new_node->wd = -1;
		new_node->active = 1;
		new_node->last_sync = time(NULL);
		new_node->last_worker_pid = -1;
		new_node->error_count = 0;
		new_node->worker_pipe_fd = -1;
		new_node->last_operation = NULL;
		new_node->next = sync_info_mem_store;
		sync_info_mem_store = new_node;

		snprintf(log_msg, sizeof(log_msg), "Added directory: %s -> %s", source, target);
		log_message(logfile, log_msg);

		new_node->wd = inotify_add_watch(inotify_fd, new_node->source, IN_CREATE | IN_MODIFY | IN_DELETE);
		if (new_node->wd == -1) {
			// No watch descriptor -- something came up and source cannot be monitored
			snprintf(log_msg, sizeof(log_msg), "Failed to monitor %s", new_node->source);
			log_message(logfile, log_msg);

			snprintf(response, sizeof(response), "[%s] Failed to monitor %s\n", timestamp, new_node->source);
			ssize_t written = write(fss_out_fd, response, strlen(response));
			if (written == -1) {
				perror("write to fss_out_fd failed");
			}
		}
		else {
			snprintf(log_msg, sizeof(log_msg), "Monitoring started for %s", new_node->source);
			log_message(logfile, log_msg);
			snprintf(response, sizeof(response), "[%s] Added directory: %s -> %s\n[%s] Monitoring started for %s\n",
					 timestamp, source, target, timestamp, new_node->source);
			ssize_t written = write(fss_out_fd, response, strlen(response));
			if (written == -1) {
				perror("write to fss_out_fd failed");
			}
			fsync(fss_out_fd);

			// Execute worker to sync fully the source
			start_worker_with_operation(source, target, "ALL", "FULL");
		}
	}

	else if (strcmp(cmd, "status") == 0) {
		snprintf(log_msg, sizeof(log_msg), "Status requested for %s", source);
		log_message(logfile, log_msg);

		SyncInfo *curr = sync_info_mem_store;
		int found = 0;
		while (curr) {
			// Search for source in sync list so as to print the status
			if (!strcmp(curr->source, source)) {
				char last_sync_time[20];
				struct tm *last_t = localtime(&curr->last_sync);
				strftime(last_sync_time, sizeof(last_sync_time), "%Y-%m-%d %H:%M:%S", last_t);

				snprintf(response, sizeof(response),
						 "[%s] Status requested for %s\n"
						 "Directory: %s\n"
						 "Target: %s\n"
						 "Last Sync: %s\n"
						 "Errors: %d\n"
						 "Status: %s\n",
						 timestamp, source,
						 curr->source, curr->target,
						 last_sync_time,
						 curr->error_count,
						 curr->active ? "Active" : "Inactive");
				ssize_t written = write(fss_out_fd, response, strlen(response));
				found = 1;
				if (written == -1) {
					perror("write to fss_out_fd failed");
				}
				fsync(fss_out_fd);
				break;
			}
			curr = curr->next;
		}

		if (!found) {
			snprintf(response, sizeof(response), "[%s] Directory not monitored: %s\n", timestamp, source);
			ssize_t written = write(fss_out_fd, response, strlen(response));
			if (written == -1) {
				perror("write to fss_out_fd failed");
			}
			fsync(fss_out_fd);
		}
	}

	else if (strcmp(cmd, "cancel") == 0) {
		SyncInfo *curr = sync_info_mem_store;
		int found = 0;

		while (curr) {
			if (strcmp(curr->source, source) == 0) {
				found = 1;
				if (curr->active) {
					// Source directory exists and it is active so we stop watching it
					curr->active = 0;
					inotify_rm_watch(inotify_fd, curr->wd);

					snprintf(log_msg, sizeof(log_msg), "Monitoring stopped for %s", source);
					log_message(logfile, log_msg);

					snprintf(response, sizeof(response), "[%s] Monitoring stopped for %s\n", timestamp, source);
				}
				else {
					// Source directory exists but already inactive
					snprintf(response, sizeof(response), "[%s] Directory not monitored: %s\n", timestamp, source);
				}

				ssize_t written = write(fss_out_fd, response, strlen(response));
				if (written == -1) {
					perror("write to fss_out_fd failed");
				}
				fsync(fss_out_fd);
				break;
			}
			curr = curr->next;
		}

		if (!found) {
			// Source directory not exist in sync list
			snprintf(response, sizeof(response), "[%s] Directory not monitored: %s\n", timestamp, source);
			ssize_t written = write(fss_out_fd, response, strlen(response));
			if (written == -1) {
				perror("write to fss_out_fd failed");
			}
			fsync(fss_out_fd);
		}
	}

	else if (strcmp(cmd, "sync") == 0) {
		SyncInfo *curr = sync_info_mem_store;
		int found = 0;
		while (curr) {
			if (strcmp(curr->source, source) == 0) {
				found = 1;

				// Check if there's an active worker for this directory
				if (curr->active) {
					snprintf(response, sizeof(response), "[%s] Sync already in progress: %s\n", timestamp, source);
					ssize_t written = write(fss_out_fd, response, strlen(response));
					if (written == -1) {
						perror("write to fss_out_fd failed");
					}
					fsync(fss_out_fd);
					return;
				}

				snprintf(log_msg, sizeof(log_msg), "Syncing directory: %s -> %s", source, curr->target);
				log_message(logfile, log_msg);

				int written = snprintf(response, sizeof(response), "[%s] Syncing directory: %s -> %s\n", timestamp, source, curr->target);

				// Add to inotify watch
				curr->wd = inotify_add_watch(inotify_fd, curr->source,
					IN_CREATE | IN_MODIFY | IN_DELETE);

				start_worker_with_operation(source, curr->target, "ALL", "FULL");

				curr->active = 1;

				written += snprintf(response + written, sizeof(response) - written, "[%s] Sync completed %s -> %s Errors:%d\n",
									timestamp, source, curr->target, curr->error_count);
				ssize_t written_in_fss_out = write(fss_out_fd, response, strlen(response));
				if (written_in_fss_out == -1) {
					perror("write to fss_out_fd failed");
				}
				fsync(fss_out_fd);

				return;
			}
			curr = curr->next;
		}

		if (!found) {
			snprintf(response, sizeof(response), "[%s] Directory not monitored: %s\n", timestamp, source);
			ssize_t written = write(fss_out_fd, response, strlen(response));
			if (written == -1) {
				perror("write to fss_out_fd failed");
			}
			fsync(fss_out_fd);
		}
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

		while (active_workers > 0) {
			sleep(1);
		}

		// Process remained tasks in the queue
		while (task_queue) {
			WorkerQueueItem *task = task_queue;
			start_worker_with_operation(task->source, task->target,
										task->filename, task->operation);
			task_queue = task_queue->next;
			free(task->source);
			free(task->target);
			free(task->filename);
			free(task->operation);
			free(task);
		}

		free_sync_info_list(sync_info_mem_store);

		exit(EXIT_SUCCESS);
	}
	else {
		snprintf(response, sizeof(response), "[%s] Unknown command: %s\n", timestamp, cmd);
		ssize_t written = write(fss_out_fd, response, strlen(response));
		if (written == -1) {
			perror("write to fss_out_fd failed");
		}
		fsync(fss_out_fd);
	}
}


int main(int argc, char *argv[]) {
	logfile = "manager.log";
	char *config_file = NULL;
	
	setlinebuf(stdout);

	int i = 1;
	if (argc < 5) {
		fprintf(stderr, "Usage: %s -l <logfile> -c <sync_info_mem_store> [-n <workers>]\n", argv[0]);
		exit(EXIT_FAILURE);
	}
	while (i < argc) {
		if (strcmp(argv[i], "-l") == 0) {
			if (i + 1 < argc) {
				logfile = argv[i + 1];
				i += 2;
			}
			else {
				fprintf(stderr, "Missing filename for -l option\n");
				exit(EXIT_FAILURE);
			}
		}
		else if (strcmp(argv[i], "-c") == 0) {
			if (i + 1 < argc) {
				config_file = argv[i + 1];
				i += 2;
			}
			else {
				fprintf(stderr, "Missing filename for -c option\n");
				exit(EXIT_FAILURE);
			}
		}
		else if (strcmp(argv[i], "-n") == 0) {
			if (i + 1 < argc) {
				worker_limit = atoi(argv[i + 1]);
				i += 2;
			}
		}
		else {
			fprintf(stderr, "Unknown option: %s\n", argv[i]);
			fprintf(stderr, "Usage: %s -l <logfile> -c <sync_info_mem_store> [-n <workers>]\n", argv[0]);
			exit(EXIT_FAILURE);
		}
	}

	create_named_pipes();

	setup_inotify();
	
	signal(SIGCHLD, sigchld_handler);

	clean_logs(logfile);
	parse_config(config_file);
	SyncInfo *curr = sync_info_mem_store;
	while (curr) {
		char log_buffer[1000];

		curr->wd = inotify_add_watch(inotify_fd, curr->source, IN_CREATE | IN_MODIFY | IN_DELETE);
		if (curr->wd == -1) {
			printf("Failed to monitor %s\n", curr->source);
			snprintf(log_buffer, sizeof(log_buffer), "Failed to monitor %s", curr->source);
			log_message(logfile, log_buffer);
		}
		else {
			snprintf(log_buffer, sizeof(log_buffer), "Added directory: %s -> %s", curr->source, curr->target);
			log_message(logfile, log_buffer);
			printf("Monitoring started for %s\n", curr->source);
			snprintf(log_buffer, sizeof(log_buffer), "Monitoring started for %s", curr->source);
			log_message(logfile, log_buffer);

			start_worker_with_operation( curr->source, curr->target, "ALL", "FULL");
		}
		curr = curr->next;
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
		int max_fd = -1;

		if (inotify_fd != -1) {
			FD_SET(inotify_fd, &read_fds);
			if (inotify_fd > max_fd) max_fd = inotify_fd;
		}

		if (fss_in_fd != -1) {
			FD_SET(fss_in_fd, &read_fds);
			if (fss_in_fd > max_fd) max_fd = fss_in_fd;
		}

		if (max_fd == -1) {
			sleep(1);
			continue;
		}

		int sel_ret = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
		if (sel_ret == -1) {
			if (errno == EINTR) continue;
			perror("select");
			continue;
		}

		// Handle filesystem events
		if (inotify_fd != -1 && FD_ISSET(inotify_fd, &read_fds)) {
			handle_inotify_events();
		}

		// Handle commands from console
		if (fss_in_fd != -1 && FD_ISSET(fss_in_fd, &read_fds)) {
			char command[256];
			ssize_t bytes = read(fss_in_fd, command, sizeof(command) - 1);
			if (bytes > 0) {
				command[bytes] = '\0';
				if (command[strlen(command) - 1] == '\n') {
					command[strlen(command) - 1] = '\0';
				}
				process_command(command, logfile, fss_in_fd, fss_out_fd);
			}
			else if (bytes == 0) {
				close(fss_in_fd);
				fss_in_fd = open("fss_in", O_RDONLY);
				if (fss_in_fd == -1) {
					perror("Failed to reopen fss_in");
				}
			}
			else {
				perror("read from fss_in");
			}
		}
	}
}
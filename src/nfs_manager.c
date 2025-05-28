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
#include <sys/stat.h>
#include <netdb.h>


typedef struct sync_info SyncInfo;

typedef struct sync_queue_task SyncTask;

// list to keep all the sync pairs
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

// Circular queue to add the tasks (specific files to get copied from the src dir to the target dir) 
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

// Queue task definitions
static SyncTask *task_buffer = NULL;
static int buffer_head = 0;
static int buffer_tail = 0;
static int buffer_count = 0;

static pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER; // mutex to protect buffer/queue operations
static pthread_cond_t not_empty = PTHREAD_COND_INITIALIZER;  // cond variable to wait until queue is not empty
static pthread_cond_t not_full = PTHREAD_COND_INITIALIZER; // cond variable to wait until queue is not full

static char manager_logfile[60];
static int worker_limit = 5;
static int port_number = 0;
static int buffer_size = 0;

static int total_tasks = 0;
static int completed_tasks = 0;
static pthread_mutex_t task_done_mutex = PTHREAD_MUTEX_INITIALIZER; // mutex to protect the total_tasks and completed_tasks variables
static pthread_cond_t all_tasks_done = PTHREAD_COND_INITIALIZER; // cond variable to wait until all tasks are done

static int shutting_down = 0;

// Function to log result from a worker thread task
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

// Function to clean manager log file from previous execution
static void clean_logfile(const char *logfile) {
	FILE *fp = fopen(logfile, "w"); // Open existing file with "write" command so that it will get empty
	if (!fp) {
		fprintf(stderr, "Failed to open logfile %s: %s", logfile, strerror(errno));
		return;
	}
	fclose(fp); // close the file
}


// Producer
void enqueue_task(SyncTask task) {
	pthread_mutex_lock(&buffer_mutex); // lock the buffer mutex (no other thread can operate in buffer until current thread finishes)
	while(buffer_count >= buffer_size) { // if queue is full
		pthread_cond_wait(&not_full, &buffer_mutex); // Wait until space is available in buffer
	}
	task_buffer[buffer_tail] = task;
	buffer_tail = (buffer_tail + 1) % buffer_size; // move tail of the queue in a circular way
	buffer_count++;
	pthread_mutex_lock(&task_done_mutex); // lock mutex for task counts
	total_tasks++;
	pthread_mutex_unlock(&task_done_mutex); // unlock the mutex for the task counts
	pthread_cond_signal(&not_empty); // We added the task, so queue is not empty (signal for any thread which tries to dequeue a task)
	pthread_mutex_unlock(&buffer_mutex); // I finished, next thread can now operate in the task queue
}

// Consumer
SyncTask dequeue_task() {
	pthread_mutex_lock(&buffer_mutex); // lock buffer for exclusive access
	while(buffer_count <= 0) { // if queue is empty
		if (shutting_down) { // if there is a signal from the manager to shut down
			pthread_mutex_unlock(&buffer_mutex);
			pthread_exit(NULL);  // exit the thread if we're shutting down
		}
		pthread_cond_wait(&not_empty, &buffer_mutex); // wait until buffer is not empty
	}
	SyncTask task = task_buffer[buffer_head];
	buffer_head = (buffer_head + 1) % buffer_size; // move the head of the queue in a circular way
	buffer_count--;

	pthread_cond_signal(&not_full); // I just removed a task from the queue so it is definitely not full. Signal the producer thread that there is space now
	pthread_mutex_unlock(&buffer_mutex); // unlock buffer
	return task;
}

// Function to parse the given config file at the start of the manager
static void parse_config_file(char *config) {
	FILE *fp = fopen(config, "r");
    if (!fp) {
		fprintf(stderr, "Error opening config file %s: %s\n", config, strerror(errno));
        exit(EXIT_FAILURE);
    }

	char line[200];
	while (fgets(line, sizeof(line), fp)) {
		char src[100], target[100];

		if (sscanf(line, "%s %s", src, target) != 2) {
            fprintf(stderr, "Incorect config file: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

		char src_path[50], src_host[50], target_path[50], target_host[50];
        int src_port, target_port;

		// parse source directory, host and port

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

		// parse target directory, host and port

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


		// Create new node in sync_info_mem_store to store the current sync configuration

		SyncInfo *node = malloc(sizeof(*node));
        if (node == NULL) {
            fprintf(stderr, "Error in memory allocation: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

		strncpy(node->source_dir, src_path, sizeof(node->source_dir));
        strncpy(node->source_host, src_host, sizeof(node->source_host));
        node->source_port = src_port;
        strncpy(node->target_dir, target_path, sizeof(node->target_dir));
		struct stat st = {0};
		if (stat(node->target_dir, &st) == -1) { // if target dir does not exist -> create it
			if (mkdir(node->target_dir, 0755) < 0) {
				fprintf(stderr, "Error in creating directory: %s\n", strerror(errno));
				return;
			}
		}

        strncpy(node->target_host, target_host, sizeof(node->target_host));
        node->target_port = target_port;

        node->active = 1;
        node->last_sync = 0;
        node->error_count = 0;
        node->next = sync_info_mem_store;
        sync_info_mem_store = node;
	}
	fclose(fp);
}

// Worker thread to sync available task in queue
void *worker_thread(void *arg) {
	while(1) {
		SyncTask curr_task = dequeue_task();

		// Search in sync_info_mem_store to check if the source directory is active
		int is_active = 0;
        SyncInfo *curr = sync_info_mem_store;
        while (curr != NULL) {
            if (strcmp(curr_task.source_dir, curr->source_dir) == 0) {
                if (curr->active) {
                    is_active = 1;
                }
                break;
            }
            curr = curr->next;
        }

        if (!is_active) {
            pthread_mutex_lock(&task_done_mutex);
            completed_tasks++;
            if (completed_tasks == total_tasks) {
                pthread_cond_signal(&all_tasks_done);
            }
            pthread_mutex_unlock(&task_done_mutex);
            continue; // source dir has been cancelled. Do not continue with the sync
        }

		struct sockaddr_in addr;
		int src_socket, target_socket;

		// source socket
		if((src_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) { // We use TCP protocol which is safer than UDP
			fprintf(stderr, "Failed to create socket to listen for source dir: %s\n", strerror(errno));
			continue;
		}

		addr.sin_family = AF_INET;
		addr.sin_port = htons(curr_task.source_port);

		inet_pton(AF_INET, curr_task.source_host, &addr.sin_addr); //Convert presentation format address to network format

		if(connect(src_socket, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
			fprintf(stderr, "Failed to connect to source socket\n");
			close(src_socket);
			continue;
		}

		// target socket
		if((target_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) { // We use TCP protocol which is safer than UDP
			continue;
		}

		addr.sin_family = AF_INET;
		addr.sin_port = htons(curr_task.target_port);

		inet_pton(AF_INET, curr_task.target_host, &addr.sin_addr); //Convert presentation format address to network format

		if(connect(target_socket, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
			fprintf(stderr, "Failed to connect to target socket\n");
			close(target_socket);
			continue;
		}

		// PULL

		char pull_src[200];
		snprintf(pull_src, sizeof(pull_src), "PULL %s/%s\n", curr_task.source_dir, curr_task.filename);
		send(src_socket, pull_src, strlen(pull_src), 0); // send pull command to client

		// nfs client sends response <filesize><space><data…>

		// first we read the size of the file:
		char file_size_buf[100];
		long unsigned int i = 0;
		char ch;
		// I read character by character the socket response from PULL command until I find space and then I convert the given string to an integer which is the filesize
		while (i < sizeof(file_size_buf) - 1) {
			int r = recv(src_socket, &ch, 1, 0); // read from the socket one byte at a time
			if (r <= 0) {
				log_sync_result(curr_task, "PULL", "ERROR", "Failed to read file size");
				close(src_socket);
				continue;
			}
			if (ch == ' ') {
				break;
			}
			file_size_buf[i++] = ch;
		}
		file_size_buf[i] = '\0';

		long filesize = atol(file_size_buf);

		if(filesize < 0) {
			log_sync_result(curr_task, "PULL", "ERROR", "Source response format not appropriate");
			close(src_socket);
			continue;
		}

		char *file_data;
		if((file_data = malloc(filesize * sizeof(char))) == NULL) {
			log_sync_result(curr_task, "PULL", "ERROR", "Fail to allocate memory");
			close(src_socket);
			continue;
		}

		// Now we read the actual data
		long parsed_data = 0;
		while(parsed_data < filesize) {
			// The data are sent in chunks so we cannot just receive them and push them to the detsination file
			int new_data = recv(src_socket, file_data + parsed_data, filesize - parsed_data, 0); // write new data in the file_data starting from the "parsed_data" position
			if(new_data <= 0)
				break;
			parsed_data += new_data;
		}

		char detail_to_log[100];
		snprintf(detail_to_log, sizeof(detail_to_log), "%ld bytes pulled", parsed_data);
		log_sync_result(curr_task, "PULL", "SUCCESS", detail_to_log);

		// PUSH

		char truncate_to_push[200];
		// first we send -1 chunk so that the client will truncate the file
		snprintf(truncate_to_push, sizeof(truncate_to_push), "PUSH %s/%s -1\n", curr_task.target_dir, curr_task.filename);
		send(target_socket, truncate_to_push, strlen(truncate_to_push), 0);

		//send data of source dir to target dir in chucks
		long long data_sent = 0;
		while(data_sent < filesize) {
			unsigned int chunk_size = (filesize - data_sent < 1024) ? filesize - data_sent : 1024; // we use 1 KB as the default chuck size to load
			char push_command[2000];
			snprintf(push_command, sizeof(push_command), "PUSH %s/%s %d ", curr_task.target_dir, curr_task.filename, chunk_size);
			char *full_message = malloc(strlen(push_command) + chunk_size); // full message contains the PUSH command and the data of the current chunk
			memcpy(full_message, push_command, strlen(push_command)); // write push message
			memcpy(full_message + strlen(push_command), file_data + data_sent, chunk_size); // write the data of the current chunk
			send(target_socket, full_message, strlen(push_command) + chunk_size, 0);
			free(full_message);
			data_sent += chunk_size;
		}

		// send 0 which means that there are no more data to push
		char no_more_data_to_push[200];
		snprintf(no_more_data_to_push, sizeof(no_more_data_to_push), "PUSH %s/%s 0\n", curr_task.target_dir, curr_task.filename);
		send(target_socket, no_more_data_to_push, strlen(no_more_data_to_push), 0);
		snprintf(detail_to_log, sizeof(detail_to_log), "%lld bytes pushed", data_sent);
		log_sync_result(curr_task, "PUSH", "SUCCESS", detail_to_log);

		free(file_data);
		close(src_socket);
		close(target_socket);

		pthread_mutex_lock(&task_done_mutex); // lock the mutex for the operations in tasks counters
		completed_tasks++;
		if (completed_tasks == total_tasks) {
			pthread_cond_signal(&all_tasks_done);  // Notify main thread that all tasks in the queue are done
		}
		pthread_mutex_unlock(&task_done_mutex); // unlock the mutex of task operations
	}
	return NULL;
}

void sync_pair_files(SyncInfo *curr, int connection_fd) { // I pass not only the sync info node but also the socket descriptor for communication with console
	if (!curr->active) // sync is inactive (pair has been canceled)
		return;
	
	char list_of_files[100][100]; // maximum 100 files per dir

	struct sockaddr_in addr;
	int socket_ = socket(AF_INET, SOCK_STREAM, 0); // create a socket to connect with the corresponding client of the source dir
	if(socket_ < 0) {
		fprintf(stderr, "Failed to create to socket with TCP protocol: %s\n", strerror(errno));
		return;
	}

	addr.sin_family = AF_INET;
	addr.sin_port = htons(curr->source_port);
	if(inet_pton(AF_INET, curr->source_host, &addr.sin_addr) <= 0) {
		fprintf(stderr, "Failed to convert presentation format address to network format: %s\n", strerror(errno));
		close(socket_);
		return;
	}

	if(connect(socket_, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "Failed to connect to source socket: %s\n", strerror(errno));
		close(socket_);
		return;
	}

	// Ask client (through the socket) to send all the files of the source dir (command LIST)
	char list_command[150];
	snprintf(list_command, sizeof(list_command), "LIST %s\n", curr->source_dir);
	send(socket_, list_command, strlen(list_command), 0);

	int count_files = 0;
	FILE *fp = fdopen(socket_, "r");
	if(fp == NULL) {
		fprintf(stderr, "Failed to open socket");
		close(socket_);
		return;
	}

	// Save all the files (that the client sent) to a list_of_files array
	char temp_line[100];
	while(fgets(temp_line, sizeof(temp_line), fp)) {
		if (strcmp(temp_line, ".\n") == 0 || strcmp(temp_line, ".\r\n") == 0) {
			break;
		}
		else {
			temp_line[strcspn(temp_line, "\r\n")] = '\0';
			if(count_files < 100) {
				strncpy(list_of_files[count_files], temp_line, 100);
				count_files++;
			}
			else {
				fprintf(stderr, "No more files can be processed. Limit reached.\n");
				break;
			}
		}
	}
	fclose(fp);
	if(count_files == 0) {
		fprintf(stdout, "No files to process from dir: %s\n", curr->source_dir);
		return;
	}

	for(int i = 0 ; i < count_files ; i++) {
		// For each file of the source dir create a task and add it in the queue to be processed
		SyncTask curr_task;

		strncpy(curr_task.source_host, curr->source_host, sizeof(curr_task.source_host));
		curr_task.source_port = curr->source_port;
		strncpy(curr_task.source_dir, curr->source_dir, sizeof(curr_task.source_dir));
		strncpy(curr_task.filename, list_of_files[i], sizeof(curr_task.filename));
		strncpy(curr_task.target_host, curr->target_host, sizeof(curr_task.target_host));
		curr_task.target_port = curr->target_port;
		strncpy(curr_task.target_dir, curr->target_dir, sizeof(curr_task.target_dir));

		enqueue_task(curr_task);

		FILE *fp = fopen(manager_logfile, "a");
		if(fp == NULL) {
			fprintf(stderr, "Failed to open manager logfile with \'a\': %s", strerror(errno));
			break;
		}
		time_t now = time(NULL);
		struct tm *t = localtime(&now);
		char response[500];
		sprintf(response, "[%04d-%02d-%02d %02d:%02d:%02d] Added file: %s/%s@%s:%d -> %s/%s@%s:%d\n",
				t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
				t->tm_hour, t->tm_min, t->tm_sec,
				curr_task.source_dir, curr_task.filename, curr_task.source_host, curr_task.source_port,
				curr_task.target_dir, curr_task.filename, curr_task.target_host, curr_task.target_port);

		fprintf(fp, "%s", response);
		
		fclose(fp);
		if(connection_fd != -1) // if current sync has been requested by the console, send response!
			send(connection_fd, response, strlen(response), 0);
	}
}

int main(int argc, char *argv[]) {
	if (argc < 9) {
        fprintf(stderr, "Usage: %s -l <logfile> -c <config_file> [-n <worker_limit>] -p <port_number> -b <bufferSize>\n", argv[0]);
        exit(EXIT_FAILURE);
    }


	// parse manager loglfile, config file, manager port and queue buffer size from the command line args

	for (int i = 1; i < argc; i += 2) {
        if (!strcmp(argv[i], "-l") && i + 1 < argc) {
            strncpy(manager_logfile, argv[i + 1], sizeof(manager_logfile));
			clean_logfile(manager_logfile);
        }
		else if (!strcmp(argv[i], "-c") && i + 1 < argc) {
			char config_file[40];
            strncpy(config_file, argv[i + 1], sizeof(config_file));
			parse_config_file(config_file); // parse the config file once we get it
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

	// create queue task buffer
	task_buffer = malloc(sizeof(SyncTask) * buffer_size);
	if (!task_buffer) {
		fprintf(stderr, "Error in memory allocation\n");
		exit(EXIT_FAILURE);
	}

	// create worker thread pool
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

	// FULL sync all the files in sync_info_mem_store (currently all files we parsed from the config file)
	SyncInfo *curr = sync_info_mem_store;
	while (curr) {
		sync_pair_files(curr, -1);
		curr = curr->next;
	}

	int server_fd, connection_fd;
	struct sockaddr_in server_addr, client_addr;
	socklen_t client_len = sizeof(client_addr);

	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if(server_fd < 0) {
		perror("socket");
		exit(EXIT_FAILURE);
	}

	int optval = 1; // flag to activate reuse option for tthe manager's socket
	setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)); // reuse server socket address (server_fd) no matter if it is occupied (avoid TIME_WAIT)

	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET; // adress in IPv4
	server_addr.sin_addr.s_addr = INADDR_ANY; // listen to all ports
	server_addr.sin_port = htons(port_number);

	// bind manager socket with the local adress
	if(bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
		perror("bind");
		exit(EXIT_FAILURE);
	}

	// listen for maximum "worker_limit" connections at the same time
	listen(server_fd, worker_limit);

	connection_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
	if(connection_fd < 0) {
		perror("accept");
		exit(EXIT_FAILURE);
	}

	while(1) {
		char command_read[300];
		ssize_t bytes_read;
		if ((bytes_read = read(connection_fd, command_read, sizeof(command_read) - 1)) > 0) {
			command_read[bytes_read] = '\0';

			// Remove trailing newline
			command_read[strcspn(command_read, "\r\n")] = '\0'; // remove \r and \n

			if (strncmp(command_read, "add ", 4) == 0) {
				char src[100], target[100];
				if (sscanf(command_read + 4, "%s %s", src, target) != 2) {
					const char *resp = "Incorrect add command\n";
					send(connection_fd, resp, strlen(resp), 0);
					send(connection_fd, "END\n", 4, 0);
					continue;
				}

				SyncInfo *node = malloc(sizeof(*node));
				if (!node) {
					send(connection_fd, "Error in memory allocation\n", 26, 0);
					send(connection_fd, "END\n", 4, 0);
					continue;
				}

				// parse source dir, host and port

				char *src_path_end = strchr(src, '@');
				if (!src_path_end) {
					send(connection_fd, "Invalid source format\n", 23, 0);
					send(connection_fd, "END\n", 4, 0);
					free(node);
					continue;
				}
				*src_path_end = '\0';

				// Check if requested directory is already synchronized
				int already_exists = 0;
				SyncInfo *curr = sync_info_mem_store;
				while (curr != NULL) {
					if (strcmp(src, curr->source_dir) == 0) {
						already_exists = 1;
						free(node);
						break;
					}
					curr = curr->next;
				}

				if (already_exists) {
					time_t now = time(NULL);
					struct tm *t = localtime(&now);
					char response[400];
					snprintf(response, sizeof(response), 
							"[%04d-%02d-%02d %02d:%02d:%02d] Already in queue: %s\n",
							t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
							t->tm_hour, t->tm_min, t->tm_sec,
							src);
					send(connection_fd, response, strlen(response), 0);
					send(connection_fd, "END\n", 4, 0);
					continue;
				}

				strncpy(node->source_dir, src, sizeof(node->source_dir));

				char *src_host_end = strchr(src_path_end + 1, ':');
				if (!src_host_end) {
					send(connection_fd, "Invalid source format\n", 23, 0);
					send(connection_fd, "END\n", 4, 0);
					free(node);
					continue;
				}
				*src_host_end = '\0';
				strncpy(node->source_host, src_path_end + 1, sizeof(node->source_host));
				node->source_port = atoi(src_host_end + 1);

				// parse target dir, host and port

				char *target_path_end = strchr(target, '@');
				if (!target_path_end) {
					send(connection_fd, "Invalid target format\n", 23, 0);
					send(connection_fd, "END\n", 4, 0);
					free(node);
					continue;
				}
				*target_path_end = '\0';
				strncpy(node->target_dir, target, sizeof(node->target_dir));

				struct stat st = {0};
				if (stat(node->target_dir, &st) == -1) {
					if (mkdir(node->target_dir, 0755) < 0) {
						perror("mkdir");
						send(connection_fd, "Failed to create target directory\n", 34, 0);
						send(connection_fd, "END\n", 4, 0);
						free(node);
						close(connection_fd);
						continue;
					}
				}

				char *target_host_end = strchr(target_path_end + 1, ':');
				if (!target_host_end) {
					send(connection_fd, "Invalid target format\n", 23, 0);
					send(connection_fd, "END\n", 4, 0);
					free(node);
					continue;
				}
				*target_host_end = '\0';
				strncpy(node->target_host, target_path_end + 1, sizeof(node->target_host));
				node->target_port = atoi(target_host_end + 1);

				node->active = 1;
				node->last_sync = 0;
				node->error_count = 0;

				// Add to front of sync_info_mem_store list
				node->next = sync_info_mem_store;
				sync_info_mem_store = node;

				// sync current pair
				sync_pair_files(node, connection_fd);

				send(connection_fd, "END\n", 4, 0);
			}
			else if (strncmp(command_read, "cancel ", 7) == 0) {
				char src_dir[100];

				if (sscanf(command_read + 7, "%s", src_dir) != 1) {
					const char *resp = "Incorrect cancel command format.\n";
					send(connection_fd, resp, strlen(resp), 0);
					send(connection_fd, "END\n", 4, 0);
					close(connection_fd);
					continue;
				}

				// Find the given dir in sync_info_mem_store list
				int cancelled = 0;
				SyncInfo *curr = sync_info_mem_store;
				while (curr != NULL) {
					if (!strcmp(curr->source_dir, src_dir)) {
						curr->active = 0;
						cancelled = 1;
						break;
					}
					curr = curr->next;
				}

				// Print corresponding message depending whether we found the dir or not in the sync_info_mem_store
				char response[400];
				if (cancelled) {
					time_t now = time(NULL);
					struct tm *t = localtime(&now);
					sprintf(response, "[%04d-%02d-%02d %02d:%02d:%02d] Synchronization stopped for %s@%s:%d\n",
							t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
							t->tm_hour, t->tm_min, t->tm_sec,
							src_dir, curr->source_host, curr->source_port);
				} else {
					time_t now = time(NULL);
					struct tm *t = localtime(&now);
					sprintf(response, "[%04d-%02d-%02d %02d:%02d:%02d] Directory not being synchronized: %s\n",
							t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
							t->tm_hour, t->tm_min, t->tm_sec,
							src_dir);
				}
				send(connection_fd, response, strlen(response), 0);
				send(connection_fd, "END\n", 4, 0);

			}
			else if (strcmp(command_read, "shutdown") == 0) {

				char response[600];
				time_t now = time(NULL);
				struct tm *t = localtime(&now);
				snprintf(response, sizeof(response),
					"[%04d-%02d-%02d %02d:%02d:%02d] Shutting down manager...\n",
					t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);

				send(connection_fd, response, strlen(response), 0);


				// The worker threads should process all remaining tasks in the queue and join
				t = localtime(&now);
				snprintf(response, sizeof(response),
					"[%04d-%02d-%02d %02d:%02d:%02d] Waiting for all active workers to finish.\n"
					"[%04d-%02d-%02d %02d:%02d:%02d] Processing remaining queued tasks.\n",
					t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec,
					t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);

				send(connection_fd, response, strlen(response), 0);

				pthread_mutex_lock(&task_done_mutex);
				while (completed_tasks < total_tasks) { // wait for all the tasks to finish
					pthread_cond_wait(&all_tasks_done, &task_done_mutex);
				}
				pthread_mutex_unlock(&task_done_mutex);

				shutting_down = 1;

				// Wake all waiting worker threads
				pthread_cond_broadcast(&not_empty);

				for (int i = 0; i < worker_limit; i++) {
					pthread_cancel(worker_threads[i]); // terminates infinite-loop threads
					pthread_join(worker_threads[i], NULL);
				}

				t = localtime(&now);
				snprintf(response, sizeof(response),
					"[%04d-%02d-%02d %02d:%02d:%02d] Manager shutdown complete\n",
					t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);

				send(connection_fd, response, strlen(response), 0);
				
				send(connection_fd, "END\n", 4, 0);

				// Free sync info nodes
				SyncInfo *curr = sync_info_mem_store;
				while (curr != NULL) {
					SyncInfo *next = curr->next;
					free(curr);
					curr = next;
				}
				sync_info_mem_store = NULL;

				free(worker_threads);
				free(task_buffer);
				close(server_fd);
				close(connection_fd);

				return 0;
			}
			else {
				const char *response = "Unknown command\n";
				send(connection_fd, response, strlen(response), 0);
				send(connection_fd, "END\n", 4, 0);
			}
		}
	}

	close(connection_fd);

	return 0;
}
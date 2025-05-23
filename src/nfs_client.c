/* File: nfs_client.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>


static void handle_list(int connfd, char *dir) {
	DIR *dirptr = opendir(dir);
	if(dirptr == NULL) {
		if (write(connfd, ".\n", 2) < 0)
			// There is nothing to send to the socket
			perror("write");
		return;
	}

	struct dirent *file;
	while((file = readdir(dirptr)) != NULL) {
		if (strcmp(file->d_name, ".") != 0 && strcmp(file->d_name, "..") != 0) {
			char buffer[300];
			snprintf(buffer, sizeof(buffer), "%s\n", file->d_name);
			if (write(connfd, buffer, strlen(buffer)) < 0)
				perror("write");
		}
	}
	if(write(connfd, ".\n", 2) < 0) {
		perror("write");		
	}
	closedir(dirptr);
}


static void handle_pull(int connfd, char *filepath) {
	int fd = open(filepath, O_RDONLY);
	if(fd < 0) {
		dprintf(connfd, "-1 %s\n", strerror(errno));
		return;
	}

	struct stat st;
	fstat(fd, &st);
	long filesize = st.st_size;

	char buffer[100];
	int len = snprintf(buffer, sizeof(buffer), "%ld ", filesize);
	if(write(connfd, buffer, len) < 0)
		perror("write");
	char read_buffer[1024];
	ssize_t bytes_read;

	while((bytes_read = read(fd, read_buffer, sizeof(read_buffer))) > 0) {
		send(connfd, read_buffer, bytes_read, 0);
	}
	close(fd);
}

static void handle_push(int connfd, char *line, FILE **out_fp) {
    char command[8], filepath[128];
    int chunk_size;

    // Parse only the command and file path and chunk size
    if (sscanf(line, "%s %s %d", command, filepath, &chunk_size) != 3) {
        fprintf(stderr, "Invalid PUSH command format\n");
        return;
    }

    if (chunk_size == -1) {
        *out_fp = fopen(filepath, "w");
        if (*out_fp == NULL) {
            fprintf(stderr, "Could not open file %s for writing\n", filepath);
        }
        return;
    } else if (chunk_size == 0) {
        if (*out_fp != NULL) {
            fclose(*out_fp);
            *out_fp = NULL;
        }
        return;
    }

    // Find where the actual binary data starts in `line`
    char *start_data = strchr(line, '\n');
    int header_len = (start_data != NULL) ? (start_data - line + 1) : strlen(line);

    int remaining = chunk_size;

    // If some of the binary data was already read in `line`, write it now
    if (start_data && *(start_data + 1) != '\0') {
        int pre_read_bytes = strlen(start_data + 1);
        fwrite(start_data + 1, 1, pre_read_bytes, *out_fp);
        remaining -= pre_read_bytes;
    }

    // Read the remaining bytes from the socket
    while (remaining > 0) {
        char buf[1024];
        int to_read = (remaining < sizeof(buf)) ? remaining : sizeof(buf);
        int bytes_read = recv(connfd, buf, to_read, MSG_WAITALL);
        if (bytes_read <= 0) {
            fprintf(stderr, "Unexpected end of stream during PUSH\n");
            break;
        }

        fwrite(buf, 1, bytes_read, *out_fp);
        fflush(*out_fp);
        remaining -= bytes_read;
    }
}



ssize_t read_line_from_socket(int sockfd, char *buf, size_t max_len) {
    size_t total_read = 0;
    while (total_read < max_len - 1) {
        char c;
        ssize_t n = recv(sockfd, &c, 1, 0);
        if (n <= 0) break;

        buf[total_read++] = c;
        if (c == '\n') break;
    }
    buf[total_read] = '\0';
    return total_read;
}

static void *handle_connection(void *arg) { // pthread required "void *function(void *arg)" function type
	int connfd = *(int *)arg;
	free(arg);  // Free the dynamically allocated memory in heap

	char line[200];

	FILE *out_fp = NULL;

	while (1) {
		char line[200];
		ssize_t n = read_line_from_socket(connfd, line, sizeof(line));
		if (n <= 0) break;

		char command[20], arg1[100];
		sscanf(line, "%s %s", command, arg1);

		if (!strcmp(command, "LIST")) {
			handle_list(connfd, arg1);
		}
		else if (!strcmp(command, "PULL")) {
			handle_pull(connfd, arg1);
		}
		else if (!strcmp(command, "PUSH")) {
			handle_push(connfd, line, &out_fp);
		}
	}

	if (out_fp != NULL) fclose(out_fp);
	close(connfd);
	return NULL;
}


int main(int argc, char *argv[]) {
	if (argc != 3 || strcmp(argv[1], "-p") != 0) {
        fprintf(stderr, "Usage: %s -p <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

	int port = atoi(argv[2]);
	int listenfd, connfd;
	struct sockaddr_in servaddr, cliaddr;
	socklen_t len;

	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	if (listenfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

	int optval = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(port);

    if (bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    listen(listenfd, 10);

    while (1) {
        len = sizeof(cliaddr);
        int *connfd_ptr = malloc(sizeof(int));
		*connfd_ptr = accept(listenfd, (struct sockaddr *)&cliaddr, &len);
		if (*connfd_ptr < 0) {
			perror("accept");
			free(connfd_ptr);
			continue;
		}

		pthread_t thread_id;
		if (pthread_create(&thread_id, NULL, handle_connection, connfd_ptr) != 0) {
			perror("pthread_create");
			close(*connfd_ptr);
			free(connfd_ptr);
		} else {
			pthread_detach(thread_id);
		}
    }

	close(listenfd);

    return 0;
}
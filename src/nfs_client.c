/* File: nfs_client.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
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
		dprintf(connfd, ".\n"); // There is nothing to send to the socket
		return;
	}

	struct dirent *file;
	while((file = readdir(dirptr)) != NULL) {
		if(strcmp(file->d_name, ".") || strcmp(file->d_name, "..")) {
			dprintf(connfd, "%s\n", file->d_name);
		}
	}
	dprintf(connfd, ".\n");
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

	dprintf(connfd, "%ld", filesize);
	char read_buffer[1024];
	ssize_t bytes_read;
	while((bytes_read = read(fd, read_buffer, sizeof(read_buffer))) > 0) {
		send(connfd, read_buffer, bytes_read, 0);
	}
	close(fd);
}

static void handle_push(int connfd, char *line) {
	char command[8], filepath[128];
    int chunk_size;
    sscanf(line, "%s %s %d", command, filepath, &chunk_size);

	static FILE *fp = NULL;

	if(chunk_size == -1) {
		fp = fopen(filepath, "w");
		if(fp == NULL) {
			fprintf(stderr, "Could not open file %s for \'w\'\n", filepath);
		}
		return;
	}
	else if (chunk_size == 0) {
		if(fp != NULL) {
			fclose(fp);
			fp = NULL;
		}
		return;
	}
	else {
		char *data = strchr(line, ' '); // parse until PUSH command
        data = strchr(data + 1, ' '); // parse until the file path
        data = strchr(data + 1, ' '); // parse until the chunk size
        if (fp)
			fwrite(data + 1, 1, chunk_size, fp); // write the data (what's left) in the file
	}
}

static void handle_connection(int connfd) {
	char line[200];
	FILE *fp = fdopen(connfd, "r+");
	if(fp == NULL) {
		fprintf(stderr, "Failed to open connection file descriptor\n");
		return;
	}

	while(fgets(line, sizeof(line), fp)) {
		char command[20], arg1[100];

		sscanf(line, "%s %s", command, arg1);

		if(!strcmp(command, "LIST")) {
			handle_list(connfd, arg1);
		}
		else if(!strcmp(command, "PULL")) {
			handle_pull(connfd, arg1);
		}
		else if(!strcmp(command, "PUSH")) {
			int chunk_size;
			sscanf(line, "%s %s %d", command, arg1, &chunk_size);
			handle_push(connfd, line);
		}
	}
	fclose(fp);
}
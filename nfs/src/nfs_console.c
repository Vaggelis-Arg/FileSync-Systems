/* File: nfs_console.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


static void log_command(const char *command_line, const char*console_logfile) {
    FILE *fp = fopen(console_logfile, "a");
    if (!fp) return;

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char timestamp[30];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", t);

    fprintf(fp, "[%s] Command %s\n", timestamp, command_line);
    fclose(fp);
}


int main(int argc, char *argv[]) {
	if(argc != 7) {
		fprintf(stderr, "Usage: %s -l <console-logfile> -h <host_IP> -p <host_port>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	char console_logfile[60];
	char host_ip[100];
	int port;

	for(int i = 1 ; i < argc ; i += 2) {
		if(!strcmp(argv[i], "-l") && i + 1 < argc) {
			strncpy(console_logfile, argv[i + 1], sizeof(console_logfile));
		}
		else if(!strcmp(argv[i], "-h") && i + 1 < argc) {
			strncpy(host_ip, argv[i + 1], sizeof(host_ip));
		}
		else if(!strcmp(argv[i], "-p") && i + 1 < argc) {
			port = atoi(argv[i + 1]);
		}
		else {
			fprintf(stderr, "Usage: %s -l <console-logfile> -h <host_IP> -p <host_port>\n", argv[0]);
			exit(EXIT_FAILURE);
		}
	}

	// Clear log file at start
    FILE *clear = fopen(console_logfile, "w");
    if (clear) fclose(clear);

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in serveraddr;
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(port);

    if (inet_pton(AF_INET, host_ip, &serveraddr.sin_addr) <= 0) {
        fprintf(stderr, "Incorrect IP: %s\n", host_ip);
        exit(EXIT_FAILURE);
    }

    if (connect(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0) {
        perror("connect");
        exit(EXIT_FAILURE);
    }

	char command_to_send[1000], response[2000];


	while(1) {
		printf("> ");
		if(!fgets(command_to_send, sizeof(command_to_send), stdin)) {
			fprintf(stderr, "Error reading command\n");
			exit(EXIT_FAILURE);
		}
		command_to_send[strcspn(command_to_send, "\n")] ='\0';

		if (strlen(command_to_send) == 0) continue;

		log_command(command_to_send, console_logfile);
		
		if(send(sockfd, command_to_send, strlen(command_to_send), 0) < 0) {
			perror("send");
			close(sockfd);
			continue;
		}


		char found_end = 0;
		while (!found_end) {
			ssize_t bytes_received = recv(sockfd, response, sizeof(response) - 1, 0);
			if (bytes_received <= 0) {
				perror("recv");
				break;
			}

			response[bytes_received] = '\0';

			char *end_marker = strstr(response, "END\n");
			if (end_marker) {
				*end_marker = '\0';  // Truncate before END
				found_end = 1;
			}

			printf("%s", response);
			fflush(stdout);
		}

		if (!strcmp(command_to_send, "shutdown")) {
            break;
        }
	}

	close(sockfd);
	return 0;
}
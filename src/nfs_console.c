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


	while(1) {
		char command_to_send[1000];
		printf("> ");
		if(!fgets(command_to_send, sizeof(command_to_send), stdin)) {
			fprintf(stderr, "Error reading command\n");
			exit(EXIT_FAILURE);
		}
		command_to_send[strcspn(command_to_send, "\n")] = 0;

		if (strlen(command_to_send) == 0) continue;

		log_command(command_to_send, console_logfile);

		int sockfd = socket(AF_INET, SOCK_STREAM, 0);
		if(sockfd < 0) {
			perror("socket");
			continue;
		}

		struct sockaddr_in serveraddr;
		memset(&serveraddr, 0, sizeof(serveraddr));
		serveraddr.sin_family = AF_INET;
		serveraddr.sin_port = htons(port);

		// Convert IP string (e.g., "127.0.0.1") to binary
		if(inet_pton(AF_INET, host_ip, &serveraddr.sin_addr) <= 0) {
			fprintf(stderr, "Incorrect IP: %s\n", host_ip);
			continue;
		}

		if(connect(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0) {
			perror("connect");
			continue;
		}
		
		if(send(sockfd, command_to_send, strlen(command_to_send), 0) < 0) {
			perror("send");
			close(sockfd);
			continue;
		}

		char response[1000];
		ssize_t received_bytes = recv(sockfd, response, sizeof(response) - 1, 0);
		if(received_bytes > 0) {
			response[received_bytes] = '\0';
			printf("%s", response);
		}

		close(sockfd);

		if (!strcmp(command_to_send, "shutdown")) {
            break;
        }
	}

	return 0;
}
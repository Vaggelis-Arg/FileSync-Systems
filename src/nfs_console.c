#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main(int argc, char *argv[]) {
	if(argc != 5) {
		fprintf(stderr, "Usage: %s -i <manager_ip> -p <port>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	char nfs_manager_ip[100];
	int port;

	for(int i = 1 ; i < argc ; i += 2) {
		if(!strcmp(argv[i], "-i") && i + 1 < argc) {
			strncpy(nfs_manager_ip, argv[i + 1], sizeof(nfs_manager_ip));
		}
		else if(!strcmp(argv[i], "-p") && i + 1 < argc) {
			port = atoi(argv[i + 1]);
		}
		else {
			fprintf(stderr, "Usage: %s -i <manager_ip> -p <port>\n", argv[0]);
			exit(EXIT_FAILURE);
		}
	}

	char command_to_send[1000];
	printf(">");
	if(!fgets(command_to_send, sizeof(command_to_send), stdin)) {
		fprintf(stderr, "Error reading command\n");
		exit(EXIT_FAILURE);
	}
	command_to_send[strcspn(command_to_send, "\n")] = 0;

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd < 0) {
		perror("socket");
		exit(EXIT_FAILURE);
	}

	struct sockaddr_in serveraddr;
	memset(&serveraddr, 0, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(port);

	// Convert IP string (e.g., "127.0.0.1") to binary
	if(inet_pton(AF_INET, nfs_manager_ip, &serveraddr.sin_addr) <= 0) {
		fprintf(stderr, "Incorrect IP: %s\n", nfs_manager_ip);
        exit(EXIT_FAILURE);
	}

	if(connect(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}
	
	if(send(sockfd, command_to_send, strlen(command_to_send), 0) < 0) {
		perror("send");
		close(sockfd);
		exit(EXIT_FAILURE);
	}

	char response[1000];
	ssize_t received_bytes = recv(sockfd, response, sizeof(response) - 1, 0);
	if(received_bytes > 0) {
		response[received_bytes] = '\0';
	}

	close(sockfd);
	return 0;
}
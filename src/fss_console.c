#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/select.h>
#include <errno.h>

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

int main(int argc, char *argv[]) {
    if (argc != 3 || strcmp(argv[1], "-l") != 0) {
        fprintf(stderr, "Usage: %s -l <console_log>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *logfile = argv[2];

	clean_logs(logfile);
    
    // Open pipes once at startup
    int fss_in_fd = open("fss_in", O_WRONLY);
    int fss_out_fd = open("fss_out", O_RDONLY | O_NONBLOCK);
    
    if (fss_in_fd == -1 || fss_out_fd == -1) {
        perror("Failed to open pipes");
        exit(EXIT_FAILURE);
    }

	printf("> ");
    fflush(stdout);

    while (1) {
        fd_set read_fds;
        int retval;

        // Watch stdin (fd 0) and fss_out_fd for input
        FD_ZERO(&read_fds);
        FD_SET(0, &read_fds);        // stdin
        FD_SET(fss_out_fd, &read_fds);

        retval = select(fss_out_fd + 1, &read_fds, NULL, NULL, NULL);
        
        if (retval == -1) {
            perror("select()");
            break;
        }

        // Check if we have user input
        if (FD_ISSET(0, &read_fds)) {
            char input[256];
            if (!fgets(input, sizeof(input), stdin)) {
                break;  // Exit on EOF
            }
            
            // Process and send command...
            input[strcspn(input, "\n")] = '\0';
            
            // Log command...
            FILE *log_fp = fopen(logfile, "a");
            if (log_fp) {
                time_t now = time(NULL);
                struct tm *t = localtime(&now);
                fprintf(log_fp, "[%04d-%02d-%02d %02d:%02d:%02d] Command: %s\n",
                        t->tm_year+1900, t->tm_mon+1, t->tm_mday,
                        t->tm_hour, t->tm_min, t->tm_sec, input);
                fclose(log_fp);
            }

            strcat(input, "\n");
            ssize_t written = write(fss_in_fd, input, strlen(input));
            if (written == -1) {
                perror("write to fss_in failed");
                continue;
            }
            fsync(fss_in_fd);
        }

        // Check for response from manager
        if (FD_ISSET(fss_out_fd, &read_fds)) {
            char response[1024];
            ssize_t bytes = read(fss_out_fd, response, sizeof(response) - 1);
            
            if (bytes > 0) {
                response[bytes] = '\0';
                printf("%s", response);
                fflush(stdout);
                
                if (strstr(response, "Manager shutdown complete") != NULL) {
                    close(fss_in_fd);
                    close(fss_out_fd);
                    return 0;
                }
            } else if (bytes == 0) {
                // Pipe closed by manager
                close(fss_in_fd);
                close(fss_out_fd);
                return 0;
            } else if (bytes == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("read from fss_out");
                break;
            }
			
			printf("> ");
            fflush(stdout);
        }
    }

    close(fss_in_fd);
    close(fss_out_fd);
    return 0;
}
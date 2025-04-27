#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/select.h>
#include <errno.h>

// Function to clean manager log file from previous execution
void clean_logs(const char *logfile) {
	FILE *fp = fopen(logfile, "w"); // Open existing file with "write" command so that it will get empty
	if (!fp)
		return;
	fclose(fp); // close the file
}

int main(int argc, char *argv[]) {
    if (argc != 3 || strcmp(argv[1], "-l") != 0) {
        fprintf(stderr, "Usage: %s -l <console_log>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *logfile = argv[2];

	clean_logs(logfile);
    
    // Open named pipes
    int fss_in_fd = open("fss_in", O_WRONLY); // Open for writing commands to fss manager
	if (fss_in_fd == -1 ) {
        perror("Failed to open fss_in named pipe");
        exit(EXIT_FAILURE);
    }

    int fss_out_fd = open("fss_out", O_RDONLY | O_NONBLOCK); // Open for non-blocking reading responses from fss manager
    if(fss_out_fd == -1) {
        perror("Failed to open fss_in named pipe");
        exit(EXIT_FAILURE);
    }

	printf("> "); // Prompt for user input
    fflush(stdout);

    while (1) {
        fd_set read_fds;
        int retval;

        // Watch stdin and fss_out_fd for input
        FD_ZERO(&read_fds);
        FD_SET(0, &read_fds);        // stdin ----> user input
        FD_SET(fss_out_fd, &read_fds); // fss_out ----> manager output

		// Wait until something occurs on either stdin or fss_out
        if (select(fss_out_fd + 1, &read_fds, NULL, NULL, NULL) == -1){
            perror("select()");
            break;
        }

        // First, check for user input (stdin)
        if (FD_ISSET(0, &read_fds)) {
            char input[100];
            if (!fgets(input, sizeof(input), stdin)) {
                break;  // Exit on EOF
            }
            
            // Remove newline character at the end of the input
            input[strcspn(input, "\n")] ='\0';
            
            // Log the command from the user to the logfile
            FILE *logfile_fp = fopen(logfile, "a");
            if(logfile_fp) {
                time_t now = time(NULL);
                struct tm *t = localtime(&now);
                fprintf(logfile_fp, "[%04d-%02d-%02d %02d:%02d:%02d] Command: %s\n",
                        t->tm_year+1900,t->tm_mon+1,t->tm_mday,
                        t->tm_hour, t->tm_min,t->tm_sec,input);
                fclose(logfile_fp);
            }

            strcat(input, "\n");
			// Send the command to the manager through fss_in
            ssize_t written = write(fss_in_fd,input,strlen(input));
            if (written == -1){
                perror("write to fss_in failed");
                continue;
            }
            fsync(fss_in_fd);
        }

        // Finally, check for response from manager
        if (FD_ISSET(fss_out_fd, &read_fds)) {
            char response[1024];
            ssize_t bytes = read(fss_out_fd, response, sizeof(response) - 1);
            
            if (bytes > 0) {
                response[bytes] ='\0';
                printf("%s", response); // Print manager response to user
                fflush(stdout); // force immediate print in stdout
                
                if (strstr(response, "Manager shutdown complete")) {
                    close(fss_in_fd);
                    close(fss_out_fd);
                    return 0;
                }
            } else if (bytes <= 0) {
                // Pipe closed by manager
                close(fss_in_fd);
                close(fss_out_fd);
                return 0;
            }
			
			printf("> ");
            fflush(stdout);
        }
    }

    close(fss_in_fd);
    close(fss_out_fd);
    return 0;
}
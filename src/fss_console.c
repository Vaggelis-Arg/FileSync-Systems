#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

int main(int argc, char *argv[]) {
    if (argc != 3 || strcmp(argv[1], "-l") != 0) {
        fprintf(stderr, "Usage: %s -l <console_log>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *logfile = argv[2];
    
    // Open pipes once at startup
    int fss_in_fd = open("fss_in", O_WRONLY);
    int fss_out_fd = open("fss_out", O_RDONLY | O_NONBLOCK);
    
    if (fss_in_fd == -1 || fss_out_fd == -1) {
        perror("Failed to open pipes");
        exit(EXIT_FAILURE);
    }

    while (1) {
        char input[256];
        printf("> ");
        fflush(stdout);
        
        if (!fgets(input, sizeof(input), stdin)) {
            break;  // Exit on EOF
        }
        
        // Remove newline if present
        input[strcspn(input, "\n")] = '\0';
        
        // Log command
        FILE *log_fp = fopen(logfile, "a");
        if (log_fp) {
            time_t now = time(NULL);
            struct tm *t = localtime(&now);
            fprintf(log_fp, "[%04d-%02d-%02d %02d:%02d:%02d] Command: %s\n",
                    t->tm_year+1900, t->tm_mon+1, t->tm_mday,
                    t->tm_hour, t->tm_min, t->tm_sec, input);
            fclose(log_fp);
        }

        // Add newline for the manager
        strcat(input, "\n");
        ssize_t written = write(fss_in_fd, input, strlen(input));
        if (written == -1) {
            perror("write to fss_in failed");
            continue;
        }
        fsync(fss_in_fd);
        
        // Read response
        char response[1024];
        ssize_t bytes;
        while ((bytes = read(fss_out_fd, response, sizeof(response) - 1)) > 0) {
            response[bytes] = '\0';
            printf("%s", response);
            fflush(stdout);
            if (strstr(response, "Manager shutdown complete") != NULL) {
                close(fss_in_fd);
                close(fss_out_fd);
                return 0;
            }
        }
    }

    close(fss_in_fd);
    close(fss_out_fd);
    return 0;
}
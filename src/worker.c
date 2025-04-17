#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>

void print_report(const char *status, const char *details, const char *errors,
	const char *source, const char *target, const char *operation) {
    int fss_out_fd = open("fss_out", O_WRONLY);
    if (fss_out_fd == -1) {
        perror("worker failed to open fss_out");
        return;
    }
    
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char timestamp[20];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", t);
    
    char report[1024];
	snprintf(report, sizeof(report), 
		"[%s] [WORKER_REPORT] [%s] [%s] [%d] [%s] [%s] [%s]\n",
		timestamp, source, target,
		getpid(), operation, status, details);

	ssize_t written = write(fss_out_fd, report, strlen(report));
	if (written == -1) {
		perror("Failed to write report to fss_out");
	}
    
    close(fss_out_fd);
    
    printf("----------------------------------------------------\n");
    printf("EXEC_REPORT_START\n");
    printf("STATUS: %s\n", status);
    printf("DETAILS: %s\n", details);
    if (errors != NULL && strlen(errors) > 0) {
        printf("ERRORS:\n%s\n", errors);
    }
    printf("EXEC_REPORT_END\n");
    printf("----------------------------------------------------\n");
}

int sync_file(const char *src, const char *dest) {
    // Create parent directory if needed
    char path[256];
    snprintf(path, sizeof(path), "%s", dest);
    char *slash = strrchr(path, '/');
    if (slash) {
        *slash = '\0';
        mkdir(path, 0755);
    }

    int sfd = open(src, O_RDONLY);
    if (sfd == -1) {
        printf("ERROR opening %s: %s\n", src, strerror(errno));
        return -1;
    }
    
    int dfd = open(dest, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (dfd == -1) {
        close(sfd);
        printf("ERROR opening %s: %s\n", dest, strerror(errno));
        return -1;
    }

    char buf[4096];
    ssize_t bytes;
    while ((bytes = read(sfd, buf, sizeof(buf))) > 0) {
        if (write(dfd, buf, bytes) != bytes) {
            close(sfd);
            close(dfd);
            printf("ERROR: %s\n", strerror(errno));
            return -1;
        }
    }

    close(sfd);
    close(dfd);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <source> <target> <filename> <operation>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *source = argv[1];
    const char *target = argv[2];
    const char *filename = argv[3];
    const char *operation = argv[4];

    int success_count = 0;
    int error_count = 0;
    char details[1024] = {0};
    char error_buffer[4096] = {0};

    if (strcmp(operation, "FULL") == 0) {
        // Full directory sync
        DIR *dir = opendir(source);
        if (!dir) {
            printf("STATUS: ERROR\nDETAILS: Failed to open directory\n");
            exit(EXIT_FAILURE);
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            char src_path[1024], dest_path[1024];
            snprintf(src_path, sizeof(src_path), "%s/%s", source, entry->d_name);
            snprintf(dest_path, sizeof(dest_path), "%s/%s", target, entry->d_name);
            
            if (sync_file(src_path, dest_path) == 0) {
                success_count++;
            } else {
                error_count++;
                char error_msg[512];
                snprintf(error_msg, sizeof(error_msg), "- File %s: %s\n", entry->d_name, strerror(errno));
                strncat(error_buffer, error_msg, sizeof(error_buffer) - strlen(error_buffer) - 1);
            }
        }
        closedir(dir);
        
        if (error_count == 0) {
            snprintf(details, sizeof(details), "%d files copied", success_count);
            print_report("SUCCESS", details, NULL, source, target, operation);
        } else if (success_count > 0) {
            snprintf(details, sizeof(details), "%d files copied, %d skipped", success_count, error_count);
            print_report("PARTIAL", details, error_buffer, source, target, operation);
        } else {
            snprintf(details, sizeof(details), "0 files copied, %d skipped", error_count);
            print_report("ERROR", details, error_buffer, source, target, operation);
        }
    } 
    else {
        char src_path[1024], dest_path[1024];
        snprintf(src_path, sizeof(src_path), "%s/%s", source, filename);
        snprintf(dest_path, sizeof(dest_path), "%s/%s", target, filename);

        if (strcmp(operation, "ADDED") == 0 || strcmp(operation, "MODIFIED") == 0) {
            if (sync_file(src_path, dest_path) == 0) {
                snprintf(details, sizeof(details), "File: %s", filename);
                print_report("SUCCESS", details, NULL, source, target, operation);
            } else {
                char error_msg[512];
                snprintf(error_msg, sizeof(error_msg), "- File %s: %s", filename, strerror(errno));
                snprintf(details, sizeof(details), "File: %s", filename);
                print_report("ERROR", details, error_msg, source, target, operation);
            }
        } 
        else if (strcmp(operation, "DELETED") == 0) {
            if (unlink(dest_path) == 0) {
                snprintf(details, sizeof(details), "File: %s", filename);
                print_report("SUCCESS", details, NULL, source, target, operation);
            } else {
                char error_msg[512];
                snprintf(error_msg, sizeof(error_msg), "- File %s: %s", filename, strerror(errno));
                snprintf(details, sizeof(details), "File: %s", filename);
                print_report("ERROR", details, error_msg, source, target, operation);
            }
        }
    }
    
    return 0;
}
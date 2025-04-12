#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>

void print_report(const char *status, const char *details, int errors) {
    printf("EXEC_REPORT_START\n");
    printf("STATUS: %s\n", status);
    printf("DETAILS: %s\n", details);
    if (errors > 0) {
        printf("ERRORS: %d\n", errors);
    }
    printf("EXEC_REPORT_END\n");
}

int sync_file(const char *src, const char *dest) {
    // Create parent directory if needed
    char path[256];
    strncpy(path, dest, sizeof(path));
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

    if (strcmp(operation, "FULL") == 0) {
        // Full directory sync
        DIR *dir = opendir(source);
        if (!dir) {
            printf("STATUS: ERROR\nDETAILS: Failed to open directory\n");
            exit(EXIT_FAILURE);
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_REG) {
                char src_path[1024], dest_path[1024];
                snprintf(src_path, sizeof(src_path), "%s/%s", source, entry->d_name);
                snprintf(dest_path, sizeof(dest_path), "%s/%s", target, entry->d_name);
                
                if (sync_file(src_path, dest_path) == 0) {
                    success_count++;
                } else {
                    error_count++;
                }
            }
        }
        closedir(dir);
		
        if (error_count == 0) {
            snprintf(details, sizeof(details), "%d files copied", success_count);
            print_report("SUCCESS", details, 0);
        } else {
            snprintf(details, sizeof(details), "%d files copied, %d failed", success_count, error_count);
            print_report("PARTIAL", details, error_count);
        }
    } 
    else {
        char src_path[1024], dest_path[1024];
        snprintf(src_path, sizeof(src_path), "%s/%s", source, filename);
        snprintf(dest_path, sizeof(dest_path), "%s/%s", target, filename);

        if (strcmp(operation, "ADDED") == 0 || strcmp(operation, "MODIFIED") == 0) {
            if (sync_file(src_path, dest_path) == 0) {
                snprintf(details, sizeof(details), "File: %s", filename);
                print_report("SUCCESS", details, 0);
            } else {
                snprintf(details, sizeof(details), "File: %s - %s", filename, strerror(errno));
                print_report("ERROR", details, 1);
            }
        } 
        else if (strcmp(operation, "DELETED") == 0) {
            if (unlink(dest_path) == 0) {
                snprintf(details, sizeof(details), "File: %s", filename);
                print_report("SUCCESS", details, 0);
            } else {
                snprintf(details, sizeof(details), "File: %s - %s", filename, strerror(errno));
                print_report("ERROR", details, 1);
            }
        }
    }
    
    return 0;
}
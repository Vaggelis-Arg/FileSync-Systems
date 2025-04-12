#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>

void sync_file(const char *src, const char *dest) {
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
        return;
    }
    
    int dfd = open(dest, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (dfd == -1) {
        close(sfd);
        printf("ERROR opening %s: %s\n", dest, strerror(errno));
        return;
    }

    char buf[4096];
    ssize_t bytes;
    while ((bytes = read(sfd, buf, sizeof(buf))) > 0) {
        write(dfd, buf, bytes);
    }

    close(sfd);
    close(dfd);
}

void delete_file(const char *path) {
    if (unlink(path) == -1) {
        printf("ERROR deleting %s: %s\n", path, strerror(errno));
    }
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

    printf("EXEC_REPORT_START\n");
    int status = 1;
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
        int file_count = 0;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_REG) {
                char src_path[1024], dest_path[1024];
                snprintf(src_path, sizeof(src_path), "%s/%s", source, entry->d_name);
                snprintf(dest_path, sizeof(dest_path), "%s/%s", target, entry->d_name);
                
                sync_file(src_path, dest_path);
                file_count++;
            }
        }
        closedir(dir);
        snprintf(details, sizeof(details), "%d files copied", file_count);
        status = 0;
    } 
    else {
        // Single file operation
        char src_path[1024], dest_path[1024];
        snprintf(src_path, sizeof(src_path), "%s/%s", source, filename);
        snprintf(dest_path, sizeof(dest_path), "%s/%s", target, filename);

        if (strcmp(operation, "ADDED") == 0 || strcmp(operation, "MODIFIED") == 0) {
            sync_file(src_path, dest_path);
            snprintf(details, sizeof(details), "File: %s", filename);
            status = 0;
        } 
        else if (strcmp(operation, "DELETED") == 0) {
            delete_file(dest_path);
            snprintf(details, sizeof(details), "File: %s", filename);
            status = 0;
        }
    }

    if (status == 0) {
        printf("STATUS: SUCCESS\n");
    } else {
        printf("STATUS: ERROR\n");
        error_count++;
    }

    printf("DETAILS: %s\n", details);
    printf("ERRORS: %d\n", error_count);
    printf("EXEC_REPORT_END\n");
    
    return status;
}
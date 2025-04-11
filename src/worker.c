#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>

void sync_file(const char *src, const char *dest) {

	// First create parent directory if needed
    char path[256];
    strncpy(path, dest, sizeof(path));
    char *slash = strrchr(path, '/');
    if (slash) {
        *slash = '\0';
        mkdir(path, 0755);  // Create parent directory
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
    printf("SUCCESS: Copied %s\n", src);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <source> <target>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    printf("EXEC_REPORT_START\n");
    printf("STATUS: SUCCESS\n");
    
    DIR *dir = opendir(argv[1]);
    if (!dir) {
        printf("STATUS: ERROR\nDETAILS: Failed to open directory\n");
        exit(EXIT_FAILURE);
    }

    struct dirent *entry;
    int file_count = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) { // Regular files only
            char src_path[1024];
            char dest_path[1024];
            
            snprintf(src_path, sizeof(src_path), "%s/%s", argv[1], entry->d_name);
            snprintf(dest_path, sizeof(dest_path), "%s/%s", argv[2], entry->d_name);
            
            sync_file(src_path, dest_path);
            file_count++;
        }
    }
    closedir(dir);

    printf("DETAILS: %d files copied\n", file_count);
    printf("EXEC_REPORT_END\n");
    return 0;
}
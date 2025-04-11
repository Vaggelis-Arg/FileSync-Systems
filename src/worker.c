#include <stdio.h>
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
        fprintf(stderr, "Usage: worker <source> <target>\n");
        return 1;
    }

    DIR *dir = opendir(argv[1]);
    if (!dir) {
        printf("ERROR: %s\n", strerror(errno));
        return 1;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) { // Regular files only
            char src_path[256], dest_path[256];
            snprintf(src_path, sizeof(src_path), "%s/%s", argv[1], entry->d_name);
            snprintf(dest_path, sizeof(dest_path), "%s/%s", argv[2], entry->d_name);
            sync_file(src_path, dest_path);
        }
    }
    closedir(dir);
    return 0;
}
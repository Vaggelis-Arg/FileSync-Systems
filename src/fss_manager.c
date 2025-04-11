/* File: fss_manager.c */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/inotify.h>

typedef struct SyncInfo {
    char *source;
    char *target;
    struct SyncInfo *next;
} SyncInfo;

void create_named_pipes() {
    // Remove existing pipes
    unlink("fss_in");
    unlink("fss_out");
    
    // Create new pipes
    if (mkfifo("fss_in", 0666) == -1) {
        perror("mkfifo fss_in failed");
        exit(EXIT_FAILURE);
    }
    if (mkfifo("fss_out", 0666) == -1) {
        perror("mkfifo fss_out failed");
        exit(EXIT_FAILURE);
    }
    printf("Named pipes created\n");
}

SyncInfo* parse_config(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) return NULL;

    SyncInfo *head = NULL;
    char line[256];
    
    while (fgets(line, sizeof(line), fp)) {
        char *source = strtok(line, " ");
        char *target = strtok(NULL, " \n");
        
        SyncInfo *new_node = malloc(sizeof(SyncInfo));
        new_node->source = strdup(source);
        new_node->target = strdup(target);
        new_node->next = head;
        head = new_node;
    }
    fclose(fp);
    return head;
}

void setup_inotify(SyncInfo *config) {
    int inotify_fd = inotify_init();
    if (inotify_fd == -1) {
        perror("inotify_init");
        exit(EXIT_FAILURE);
    }

    SyncInfo *current = config;
    while (current) {
        int wd = inotify_add_watch(inotify_fd, current->source, 
                                  IN_CREATE | IN_MODIFY | IN_DELETE);
        if (wd == -1) {
            printf("Failed to watch %s\n", current->source);
        } else {
            printf("Watching: %s (wd=%d)\n", current->source, wd);
        }
        current = current->next;
    }
}

int main() {
    create_named_pipes();
    SyncInfo *config = parse_config("../data/config.txt");
    setup_inotify(config);
    return 0;
}
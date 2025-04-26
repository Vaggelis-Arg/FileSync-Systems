/* File: worker.c */
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

// Function to print the report with the worker tag in the stdout
// (which will be redirected to the worker pipe in the manager)
void print_report(const char *status, const char *details, const char *errors,
    const char *source, const char *target, const char *operation){
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char timestamp[20];
	// Format timestamp as "YYYY-MM-DD HH:MM:SS"
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", t);
    
    printf("[%s] [WORKER_REPORT] [%s] [%s] [%d] [%s] [%s] [%s]\n",timestamp, source, target,
        getpid(), operation,status, strcmp(status, "ERROR") ? details : errors);
    
    fflush(stdout);
}

// Function to copy src file to dest file
int sync_file(const char *src, const char *dest) {

    char path[50];
    snprintf(path, sizeof(path), "%s", dest);
    char *slash = strrchr(path,'/');
    if (slash) {
		// If needed we create a parent directory for destination
        *slash ='\0';
        mkdir(path, 0755);
    }

    int src_fd = open(src, O_RDONLY);
    if (src_fd == -1) {
        return -1;
    }
    
    int dest_fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dest_fd == -1) {
        close(src_fd);
        return -1;
    }

    char buf[4000];
    ssize_t bytes;
	// Read bytes from the source file and write them to the destination
    while((bytes = read(src_fd, buf, sizeof(buf))) > 0) {
        if (write(dest_fd, buf, bytes) != bytes) {
            close(src_fd);
            close(dest_fd);
            return -1;
        }
    }

    close(src_fd);
    close(dest_fd);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <source> <target> <filename> <operation>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *source = argv[1];
    char *target = argv[2];
    char *filename = argv[3];
    char *operation = argv[4];

    int success_count = 0, error_count = 0;
    char details[1000] = {0}, error_buffer[1000] = {0};

    if (strcmp(operation, "FULL") == 0) {
		// Full directory synchronization
        DIR *dir = opendir(source);
        if (!dir) {
            exit(EXIT_FAILURE);
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
			// Skip current and previous directory entries
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
                continue;
            }

            char src_path[300], dest_path[300];
            snprintf(src_path, sizeof(src_path), "%s/%s", source, entry->d_name);
            snprintf(dest_path, sizeof(dest_path), "%s/%s", target, entry->d_name);
            
            if (sync_file(src_path, dest_path) == 0) {
                success_count++;
            } else{
                error_count++;
                char error_msg[300];
                snprintf(error_msg, sizeof(error_msg), "- File %s: %s", entry->d_name, strerror(errno));
                strncat(error_buffer, error_msg, sizeof(error_buffer) - strlen(error_buffer) - 1);
            }
        }
        closedir(dir);
        
		// Generate corresponding report
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
		// Single file operations: ADDED, MODIFIED, or DELETED
        char src_path[300], dest_path[300];
        snprintf(src_path, sizeof(src_path), "%s/%s", source, filename);
        snprintf(dest_path, sizeof(dest_path), "%s/%s", target, filename);

        if (!strcmp(operation, "ADDED") || !strcmp(operation, "MODIFIED")){
            if (sync_file(src_path, dest_path) == 0) {
                snprintf(details, sizeof(details), "File: %s", filename);
                print_report("SUCCESS", details, NULL, source, target, operation);
            } else {
                char error_msg[300];
                snprintf(error_msg, sizeof(error_msg), "File %s: %s", filename, strerror(errno));
                print_report("ERROR", NULL, error_msg, source, target, operation);
            }
        } 
        else if (strcmp(operation, "DELETED") == 0){
			// Try to delete the destination file
            if (unlink(dest_path) == 0) {
                snprintf(details, sizeof(details), "File: %s", filename);
                print_report("SUCCESS", details, NULL, source, target, operation);
            } else {
				// If deletion fails print error message
                char error_msg[300];
                snprintf(error_msg, sizeof(error_msg), "File %s: %s", filename, strerror(errno));
                print_report("ERROR", NULL, error_msg, source, target, operation);
            }
        }
		else 
		{
			return -1;
		}
    }
    
    return 0;
}
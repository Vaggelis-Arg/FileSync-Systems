/* File: fss_manager.c */
#include <fcntl.h>
#include <sys/stat.h>

void create_named_pipes() {
    // Remove existing pipes if they exist
    unlink("fss_in");
    unlink("fss_out");
    
    // Create new named pipes
    mkfifo("fss_in", 0666);
    mkfifo("fss_out", 0666);
}
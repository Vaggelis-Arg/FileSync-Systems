#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    printf("Worker PID: %d\n", getpid());
    printf("Syncing %s -> %s\n", argv[1], argv[2]);
    sleep(1); // Simulate work
    return 0;
}
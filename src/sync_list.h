/* File: sync_list.h */
#ifndef SYNC_LIST_H
#define SYNC_LIST_H

#include <stdlib.h>

typedef struct sync_info {
    char *source;
    char *target;
    time_t last_sync;
    int active;
    int error_count;
} SyncInfo;

typedef struct sync_listnode *SyncList;

// Functions to visit nodes printing their data
typedef void (*SyncPrintFunc)(SyncInfo *);

// Function to add an element at the end of the list
SyncList sync_list_append(SyncList list, SyncInfo *info);

// Function to add an element at the start of the list
SyncList sync_list_prepend(SyncList list, SyncInfo *info);

// Function to search an item into the list by source directory
SyncInfo *sync_list_search(SyncList list, const char *source);

// Function to delete an item from the list by source directory
SyncList sync_list_delete(SyncList list, const char *source);

// Function to return the size of a list
size_t sync_list_size(SyncList list);

// Function de-allocate a list
void sync_list_free(SyncList list);

// Helper function to print a list
void sync_list_print(SyncList list, SyncPrintFunc print);

// Function to merge two lists
SyncList sync_list_merge(SyncList list1, SyncList list2);

// Get the info of the given node
SyncInfo *sync_listnode_get_info(SyncList node);

// Get next list node
SyncList sync_list_get_next(SyncList list);

// Get first node of the given list
SyncList sync_list_get_first(SyncList list);

// Get last node of the given list
SyncList sync_list_get_last(SyncList list);

#endif
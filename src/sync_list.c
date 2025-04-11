/* File: sync_list.c */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "sync_list.h"

struct sync_listnode {
    SyncInfo *info;
    SyncList next;
};

SyncInfo *sync_info_create(const char *source, const char *target) {
    SyncInfo *info = malloc(sizeof(SyncInfo));
    assert(info != NULL);
    
    info->source = strdup(source);
    info->target = strdup(target);
    info->last_sync = 0;
    info->active = 1;
    info->error_count = 0;
    
    return info;
}

void sync_info_destroy(SyncInfo *info) {
    if (info != NULL) {
        free(info->source);
        free(info->target);
        free(info);
    }
}

SyncList sync_list_append(SyncList list, SyncInfo *info) {
    SyncList node = malloc(sizeof(*node));
    assert(node != NULL);
    
    node->next = NULL;
    node->info = info;

    if(list == NULL) {
        list = node;
    } else {
        SyncList cur = list;
        while(cur->next != NULL)
            cur = cur->next;
        cur->next = node;
    }
    return list;
}

SyncList sync_list_prepend(SyncList list, SyncInfo *info) {
    SyncList node = malloc(sizeof(*node));
    assert(node != NULL);

    node->info = info;
    node->next = list;

    return node;
}

SyncInfo *sync_list_search(SyncList list, const char *source) {
    SyncList cur = list;

    while(cur != NULL) {
        if(strcmp(cur->info->source, source) == 0)
            return cur->info;
        cur = cur->next;
    }
    
    return NULL;
}

SyncList sync_list_delete(SyncList list, const char *source) {
    SyncList cur = list;
    SyncList prev = NULL;
    
    while(cur != NULL) {
        if(strcmp(cur->info->source, source) == 0) {
            if(prev == NULL) {
                SyncList next = cur->next;
                sync_info_destroy(cur->info);
                free(cur);
                return next;
            } else {
                prev->next = cur->next;
                sync_info_destroy(cur->info);
                free(cur);
                return list;
            }
        }
        prev = cur;
        cur = cur->next;
    }
    return list;
}

size_t sync_list_size(SyncList list) {
    SyncList cur = list;
    size_t count = 0;

    while(cur != NULL) {
        count++;
        cur = cur->next;
    }
    
    return count;
}

void sync_list_free(SyncList list) {
    while(list != NULL) {
        SyncList temp = list;
        list = list->next;
        sync_info_destroy(temp->info);
        free(temp);
    }
}

void sync_list_print(SyncList list, SyncPrintFunc print) {
    printf("[");
    while(list != NULL) {
        print(list->info);
        list = list->next;
        if (list != NULL) printf(", ");
    }
    printf("]\n");
}

SyncList sync_list_merge(SyncList list1, SyncList list2) {
    if (list1 == NULL) return list2;
    
    SyncList temp = list1;
    while(temp->next != NULL)
        temp = temp->next;
    temp->next = list2;
    return list1;
}

SyncInfo *sync_listnode_get_info(SyncList node) {
    return node->info;
}

SyncList sync_list_get_next(SyncList list) {
    return list->next;
}

SyncList sync_list_get_first(SyncList list) {
    return list;
}

SyncList sync_list_get_last(SyncList list) {
    if(list == NULL)
        return NULL;
    while(list->next != NULL)
        list = list->next;
    return list;
}
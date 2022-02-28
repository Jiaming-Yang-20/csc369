
#pragma once
#include <stdio.h>
#include <stdlib.h>

typedef struct list_entry
{
    int frame;
    struct list_entry *next;
    struct list_entry *prev;
} list_entry;

typedef struct list
{
    list_entry *head;
    list_entry *tail;
} list;

static inline void list_entry_init(list_entry *entry)
{
    entry->next = NULL;
    entry->prev = NULL;
    entry->frame = -1; //the list entry is not put in the linked list
}

static inline void list_add_head(list *linked_list, list_entry *entry)
{
    if (linked_list->head == NULL) //no element in list
    {
        linked_list->head = entry;
        linked_list->tail = entry;
        linked_list->head->prev = NULL;
        linked_list->tail->next = NULL;
    }
    else
    {
        entry->next = linked_list->head;
        linked_list->head->prev = entry;
        linked_list->head = entry;
        linked_list->head->prev = NULL;
    }
}

//delete the entry if the entry exists
//the entry can be the head or tail or middle
static inline void list_del(list *linked_list, list_entry *entry)
{
    if (entry->frame != -1)
    { // the entry exists in the list
        if (entry->prev == NULL && entry->next == NULL)
        { //only one element in list
            linked_list->head = NULL;
            linked_list->tail = NULL;
        }
        else if (entry->prev == NULL)
        { //delete list head
            linked_list->head = entry->next;
            linked_list->head->prev = NULL;
        }
        else if (entry->next == NULL)
        { //delete list tail
            linked_list->tail = entry->prev;
            linked_list->tail->next = NULL;
        }
        else
        { //delete list body
            entry->next->prev = entry->prev;
            entry->prev->next = entry->next;
        }
    }
    entry->frame = -1;
}

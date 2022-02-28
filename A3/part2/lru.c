/*
 * This code is provided solely for the personal and private use of students
 * taking the CSC369H course at the University of Toronto. Copying for purposes
 * other than this use is expressly prohibited. All forms of distribution of
 * this code, including but not limited to public repositories on GitHub,
 * GitLab, Bitbucket, or any other online platform, whether as given or with
 * any changes, are expressly prohibited.
 *
 * Authors: Andrew Peterson, Karen Reid, Alexey Khrabrov
 *
 * All of the files in this directory and all subdirectories are:
 * Copyright (c) 2019, 2021 Karen Reid
 */

#include "pagetable.h"
#include "sim.h"
#include "list.h"
list *linked_list;
list_entry *lst_entries;

/* Page to evict is chosen using the accurate LRU algorithm.
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */
int lru_evict(void)
{ // evict the tail -- least recently used
	//TODO
	int tail_frame = linked_list->tail->frame;
	list_del(linked_list, linked_list->tail);
	return tail_frame;
}

/* This function is called on each access to a page to update any information
 * needed by the LRU algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void lru_ref(pt_entry_t *pte)
{

	//TODO
	int frame_idx = pte->frame >> PAGE_SHIFT;
	struct frame *frame_ref = &coremap[frame_idx];
	list_del(linked_list, frame_ref->lst_entry_ptr);	  //remove entry if exists
	frame_ref->lst_entry_ptr->frame = frame_idx;		  //make the corresponding entry valid
	list_add_head(linked_list, frame_ref->lst_entry_ptr); //add to the head of the linked lst
}

/* Initialize any data structures needed for this replacement algorithm. */
void lru_init(void)
{
	//TODO
	linked_list = malloc(sizeof(list));
	lst_entries = malloc(sizeof(list_entry) * memsize);
	linked_list->head = NULL;
	linked_list->tail = NULL;
	for (size_t i = 0; i < memsize; i++)
	{
		list_entry_init(&(lst_entries[i]));			  //initialize list entries, all made invalid at first
		coremap[i].lst_entry_ptr = &(lst_entries[i]); //point to correponding entries
	}
}

/* Cleanup any data structures created in lru_init(). */
void lru_cleanup(void)
{
	//TODO
	free(linked_list);
	free(lst_entries);
}

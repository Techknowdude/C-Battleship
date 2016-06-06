#pragma once

/***************************************
 * LinkedList functions.
 * Originaly created for use in the Operating Systems class.
 *
 * Author: Brandon Westmoreland
 **************************************/


typedef struct item_s1
{
    void *value;
    struct item_s1 *next;
    struct item_s1 *prev;
    pthread_mutex_t lock;
} list_tt;

// Type for a list
typedef void *list_t;

// Type for a node within a list
typedef void *list_item_t;

// Initialize an empty list
void List_Init(list_t *list);

// Destroy all the nodes in a list
// NOTE: Does not destroy data stored in nodes
void List_Destroy(list_t list);

// Returns a pointer that can be used as an iterator
// The returned value points to the item Prior to the first item in the list.
// To get an actual list item, you must call List_Next
//
// WARNING: This function will LOCK the list. Use List_Done_Iterating() to unlock.
list_item_t List_First(list_t item);

// Advanced the iterator one position in the list. 
// Will return NULL at the end of the list
list_item_t List_Next(list_item_t item);

// Insert a node After the current position in the list
// Returns a pointer to the inserted item
list_item_t List_Insert_At(list_item_t item, void *value);

// Removes the node at the current list position. 
// Does not free memory for the item stored at this position.
// Returns a pointer to the next item in the list 
// or NULL if this is the last item
// It is an error to call this function on List_First().
list_item_t List_Remove_At(list_item_t item);

// returns a pointer to the item stored at the current list position
void *List_Get_At(list_item_t item);

// Unlocks the list when done iterating. MUST BE CALLED WHEN DONE!
// We are all gentlemen here.
void List_Done_Iterating(list_t list);

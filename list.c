#include <assert.h>
#include <stdlib.h>
#include <limits.h>
#include <pthread.h>

#include "list.h"

//**********************************
void List_Init(list_t *l)
{
    assert(l != NULL);
    list_tt *list = (list_tt *)malloc(sizeof(list_tt));
    assert(list != NULL);
    list->next = NULL;
    list->prev = NULL;
    list->value = NULL;
    pthread_mutex_init(&(list->lock),NULL);

    *l = list;
}
//**********************************
// NOTE: This function destroys the list nodes, but not the
//       items stored in the list
void List_Destroy(list_t list)
{
    if (list == NULL) return;

    list_tt *root = (list_tt *)list;
    list_tt *ptr = root;
    list_tt *prev;

    // lock it!
    pthread_mutex_lock(&(root->lock));
    prev = ptr;
    ptr = ptr->next;

    while (ptr != NULL)
    {
        free(prev);
        prev = ptr;
        ptr = ptr->next;
    }

    free(prev);
    pthread_mutex_unlock(&(root->lock));
}
//**********************************
list_item_t List_First(list_t list)
{
    if (list == NULL) return NULL;

    list_tt *root = (list_tt*)list;
    pthread_mutex_lock(&(root->lock));
    return (list_item_t)list;
}
//**********************************
list_item_t List_Next(list_item_t item)
{
    list_tt *ptr = (list_tt *)item;

    if (ptr == NULL) return NULL;

    return ptr->next;
}
//**********************************
list_item_t List_Insert_At(list_item_t item, void *value)
{
    assert(item != NULL);

    list_tt *ptr = (list_tt *)item;
    list_tt *next = ptr->next;

    ptr->next = (list_tt *)malloc(sizeof(list_tt));
    assert(ptr->next != NULL);

    ptr->next->value = value;
    ptr->next->next = next;
    ptr->next->prev = ptr;
    if (next != NULL) next->prev = ptr->next;
    
    return ptr->next;
}
//**********************************
void List_Done_Iterating(list_t list)
{
    assert(list != NULL);

    list_tt * root = (list_tt *)list;
    pthread_mutex_unlock(&(root->lock));
}

//**********************************
list_item_t List_Remove_At(list_item_t item)
{
    assert(item != NULL);
    list_tt *ptr = (list_tt *)item;
    list_tt *next;

    // make sure this isn't the dummy at the beginning
    assert (ptr->prev != NULL);

    next = ptr->next;

    ptr->prev->next = ptr->next;
    if (ptr->next != NULL) ptr->next->prev = ptr->prev;

    free(ptr);

    return next;
}
//**********************************
void *List_Get_At(list_item_t item)
{
    assert(item != NULL);

    list_tt *ptr = (list_tt *)item;

    return ptr->value;
}

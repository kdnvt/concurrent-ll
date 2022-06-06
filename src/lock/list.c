#include "list.h"

struct node {
    val_t data;
    struct node *next;
    ptlock_t *lock; /* lock for this entry */
};

struct list {
    node_t *head;
    hp_pr_t *pr;
};

hp_pr_t *list_pr(list_t *list){
    return list->pr;
}

void node_free(void *arg)
{ 
}

bool list_contains(list_t *the_list, val_t val,hp_t *hp)
{
    /* lock sentinel node */
    node_t *elem = the_list->head;
    LOCK(elem->lock);
    if (!elem->next) { /* the list is empty */
        UNLOCK(elem->lock);
        return false;
    }

    node_t *prev = elem;
    while (elem->next && elem->next->data <= val) {
        if (elem->next->data == val) { /* found it */
            UNLOCK(elem->lock);
            return true;
        }
        prev = elem;
        elem = elem->next;
        LOCK(elem->lock);
        UNLOCK(prev->lock);
    }

    /* just check if the last node in the list is not equal to val */
    if (elem->data == val) { /* found */
        UNLOCK(elem->lock);
        return true;
    }

    /* not found in the list */
    UNLOCK(elem->lock);
    return false;
}

static node_t *new_node(val_t val, node_t *next)
{
    /* allocate node */
    node_t *node = malloc(sizeof(node_t));

    /* allocate lock */
    node->lock = malloc(sizeof(ptlock_t));

    /* initialize the lock */
    INIT_LOCK(node->lock);

    node->data = val;
    node->next = next;
    return node;
}

list_t *list_new()
{
    /* allocate list */
    list_t *the_list = malloc(sizeof(list_t));

    /* now need to create the sentinel node */
    the_list->head = new_node(0, NULL);
    return the_list;
}

void list_delete(list_t *the_list)
{
    /* must lock the whole list */
    node_t *elem = the_list->head;
    LOCK(elem->lock);
    if (!elem->next) { /* an empty list, just delete sentinel node */
        UNLOCK(elem->lock);
        DESTROY_LOCK(elem->lock);

        /* deallocate memory and we are done */
        free(elem->lock);
        free(elem);
    } else { /* have to go through list */
        while (elem->next) {
            /* lock everything */
            LOCK(elem->next->lock);
            elem = elem->next;
        }

        /* everything is locked, delete them */
        while (the_list->head) {
            elem = the_list->head;
            the_list->head = elem->next;

            UNLOCK(elem->lock);
            DESTROY_LOCK(elem->lock);

            free(elem->lock);
            free(elem);
        }
    }

    free(the_list);
}

int list_size(list_t *the_list)
{
    int size = 0;
    /* must lock the whole list */
    node_t *prev = the_list->head;
    LOCK(prev->lock);
    if (!prev->next) { /* the list is empty */
        UNLOCK(prev->lock);
        return size;
    }

    node_t *elem = prev->next;
    LOCK(elem->lock);
    size++;
    while (elem->next) {
        size++;
        UNLOCK(prev->lock);
        prev = elem;
        elem = elem->next;
        LOCK(elem->lock);
    }

    /* we did not find it; unlock and report failure */
    UNLOCK(elem->lock);
    UNLOCK(prev->lock);
    return size;
}

bool list_add(list_t *the_list, val_t val,hp_t *hp)
{
    /* lock sentinel node */
    node_t *elem = the_list->head;
    LOCK(elem->lock);
    if (!elem->next) { /* the list is empty */
        node_t *new_elem = new_node(val, NULL);
        elem->next = new_elem;
        UNLOCK(elem->lock);
        return true;
    }

    node_t *prev = elem;

    while (elem->next && elem->next->data <= val) {
        if (elem->next->data == val) {
            /* we already have that value, unlock and report failure */
            UNLOCK(elem->lock);
            return false;
        }
        prev = elem;
        elem = elem->next;
        LOCK(elem->lock);
        UNLOCK(prev->lock);
    }
    /* just check if the last node in the list is not equal to val */
    if (elem->data == val) { /* if equal report failure */
        UNLOCK(elem->lock);
        return false;
    }

    /* place it in between prev and elem */
    node_t *new_elem = new_node(val, elem->next);
    elem->next = new_elem;

    /* successfully added new value, unlock elem */
    UNLOCK(elem->lock);
    return true;
}

bool list_remove(list_t *the_list, val_t val,hp_t *hp)
{
    /* lock sentinel node */
    node_t *prev = the_list->head;
    LOCK(prev->lock);
    if (!prev->next) { /* the list is empty */
        UNLOCK(prev->lock);
        return false;
    }

    node_t *elem = prev->next;
    LOCK(elem->lock);
    while (elem->next && elem->data <= val) {
        if (elem->data == val) {
            /* if found, assign prev next to elem next */
            prev->next = elem->next;

            /* unlock and deallocate mem */
            UNLOCK(elem->lock);
            DESTROY_LOCK(elem->lock);
            free(elem->lock);
            free(elem);

            /* success */
            UNLOCK(prev->lock);
            return true;
        }
        UNLOCK(prev->lock);
        prev = elem;
        elem = elem->next;
        LOCK(elem->lock);
    }

    /* just check if the last node in the list is not equal to val */
    if (elem->data == val) {
        /* if found, assign prev next to elem next */
        prev->next = elem->next;

        /* unlock and deallocate mem */
        UNLOCK(elem->lock);
        DESTROY_LOCK(elem->lock);
        free(elem->lock);
        free(elem);

        /* success */
        UNLOCK(prev->lock);
        return true;
    }

    /* we did not find it; unlock and report failure */
    UNLOCK(elem->lock);
    UNLOCK(prev->lock);
    return false;
}

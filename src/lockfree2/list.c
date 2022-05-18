#include "list.h"
#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
struct node {
    int key;
    struct node *back_link;
    struct node *next;
};

struct list {
    struct node *head, *tail;
    uint32_t size;
};

struct two_ptr {
    node_t *cur;
    node_t *next;
};

#define F_MARK 2
#define F_FLAG 1
#define F_MASK 3
#define CUREQ 0
#define NEXTEQ 1

#define MARK(ptr) ((uintptr_t) ptr | F_MARK)
#define UNMARK(ptr) ((uintptr_t) ptr & ~F_MARK)
#define ISMARK(ptr) ((uintptr_t) ptr & F_MARK)

#define FLAG(ptr) ((uintptr_t) ptr | F_FLAG)
#define UNFLAG(ptr) ((uintptr_t) ptr & ~F_FLAG)
#define ISFLAG(ptr) ((uintptr_t) ptr & F_FLAG)

#define ptrof(ptr) ((node_t *) ((uintptr_t) ptr & ~F_MASK))

node_t *search(list_t *list, int key);

node_t *remove_node(list_t *list, int key);

node_t *insert(list_t *list, int key);

static struct two_ptr search_from(int key, node_t *curr_node, int cur_or_next);

static void help_marked(node_t *prev, node_t *del);

static void help_flag(node_t *prev, node_t *del);

static void try_mark(node_t *del);

static struct two_ptr try_flag(node_t *prev, node_t *target);

static node_t *new_node(int key);

/* return true if value already in the list */
bool list_contains(list_t *list, val_t val)
{
    if (search(list, val))
        return true;
    return false;
}
/* insert a new node with the given value val in the list.
 * @return true if succeed
 */
bool list_add(list_t *list, val_t val)
{
    if (insert(list, val))
        return true;
    return false;
}

/* delete a node with the given value val (if the value is present).
 * @return true if succeed
 */
bool list_remove(list_t *list, val_t val)
{
    if (remove_node(list, val))
        return true;
    return false;
}

int list_size(list_t *list)
{
    return list->size;
}

list_t *list_new()
{
    /* allocate list */
    list_t *the_list = malloc(sizeof(list_t));

    /* now need to create the sentinel node */
    the_list->head = new_node(INT_MIN);
    the_list->tail = new_node(INT_MAX);
    the_list->head->next = the_list->tail;
    the_list->size = 0;
    return the_list;
}

/* return the node with key if it exists, NULL otherwise. */
node_t *search(list_t *list, int key)
{
    node_t *cur = search_from(key, list->head, CUREQ).cur;
    if (cur->key == key)
        return cur;
    return NULL;
}

/* return true if the node with key is removed successfully.
   return NULL if the node doesn't exist,
   or the node is already removed by others. */
node_t *remove_node(list_t *list, int key)
{
    struct two_ptr pos = search_from(key, list->head, NEXTEQ);
    node_t *del_node = pos.next;
    if (del_node->key != key)
        return NULL;
    pos = try_flag(pos.cur, del_node);
    if (pos.cur) {
        help_flag(pos.cur, del_node);
    }
    if (!pos.next)
        return NULL;
    atomic_fetch_sub(&list->size, 1);
    return del_node;
}

/*  Return true if the node is inserted in list successfully.
    Return NULL if there is already a node with such key. */
node_t *insert(list_t *list, int key)
{
    struct two_ptr pos = search_from(key, list->head, CUREQ);
    if (pos.cur->key == key) {
        return NULL;
    }
    node_t *tmp = new_node(key);
    while (1) {
        node_t *prev_succ = pos.cur->next;
        if (ISFLAG(prev_succ)) {
            help_flag(pos.cur, ptrof(prev_succ));
        } else {
            tmp->next = ptrof(pos.next);
            node_t *next_node = pos.next;
            if (atomic_compare_exchange_strong(&pos.cur->next, &next_node,
                                               tmp)) {
                atomic_fetch_add(&list->size, 1);

                return tmp;
            } else {
                if (next_node == (node_t *) FLAG(pos.cur->next))
                    help_flag(pos.cur, ptrof(next_node));
                while (ISMARK(pos.cur->next))
                    pos.cur = pos.cur->back_link;
            }
        }
        pos = search_from(key, pos.cur, CUREQ);
        if (pos.cur->key == key) {
            free(tmp);
            return NULL;
        }
    }
}

static node_t *new_node(int key)
{
    node_t *cur = calloc(1, sizeof(*cur));
    cur->key = key;
    return cur;
}

static struct two_ptr search_from(int key, node_t *curr_node, int cur_or_next)
{
    node_t *next_node = ptrof(curr_node->next);
    while ((!cur_or_next && next_node->key <= key) ||
           (cur_or_next && next_node->key < key)) {
        while (
            ISMARK(next_node->next) &&
            (!ISMARK(curr_node->next) || ptrof(curr_node->next) != next_node)) {
            if (ptrof(curr_node->next) == next_node)
                help_marked(curr_node, next_node);

            next_node = ptrof(curr_node->next);
        }
        if ((!cur_or_next && next_node->key <= key) ||
            (cur_or_next && next_node->key < key)) {
            curr_node = next_node;
            next_node = ptrof(curr_node->next);
        }
    }

    return (struct two_ptr){curr_node, next_node};
}

static void help_marked(node_t *prev, node_t *del)
{
    node_t *next_node = del->next;
    del = (node_t *) FLAG(del);
    if (atomic_compare_exchange_strong(&prev->next, &del, (ptrof(next_node))))
        ;
}

static void help_flag(node_t *prev, node_t *del)
{
    del->back_link = prev;
    if (!ISMARK(del->next))
        try_mark(del);
    help_marked(prev, del);
}

static void try_mark(node_t *del)
{
    do {
        node_t *next_node = ptrof(del->next);
        atomic_compare_exchange_weak(&del->next, &next_node,
                                     (node_t *) MARK(next_node));
        if (ISFLAG(next_node))
            help_flag(del, (node_t *) ptrof(next_node));
    } while (!ISMARK(del->next));
}

static struct two_ptr try_flag(node_t *prev, node_t *target)
{
    while (1) {
        if (prev->next == (node_t *) FLAG(target))
            return (struct two_ptr){prev, NULL};
        node_t *expect = target;
        atomic_compare_exchange_strong(&prev->next, &expect,
                                       (node_t *) FLAG(target));
        if (expect == target)
            return (struct two_ptr){prev, (node_t *) 1};
        if (expect == (node_t *) FLAG(target))
            return (struct two_ptr){prev, NULL};
        while (ISMARK(prev->next))
            prev = prev->back_link;
        struct two_ptr tmp = search_from(target->key, prev, NEXTEQ);
        if (tmp.next != target)
            return (struct two_ptr){NULL, NULL};
    }
}

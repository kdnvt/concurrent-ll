/* interface for the list */

#ifndef _LIST_H_
#define _LIST_H_

#include <stdbool.h>
#include "lock.h"
#include "hp.h"

typedef intptr_t val_t;

typedef struct node node_t;
typedef struct list list_t;

/* return 0 if not found, positive number otherwise */
list_t *list_new();

/* return true if value already in the list */
bool list_contains(list_t *the_list, val_t val,hp_t *hp);

/* insert a new node with the given value val in the list.
 * @return true if succeed
 */
bool list_add(list_t *the_list, val_t val,hp_t *hp);

/* delete a node with the given value val (if the value is present).
 * @return true if succeed
 */
bool list_remove(list_t *the_list, val_t val,hp_t *hp);

hp_pr_t *list_pr(list_t *list);

void node_free(void *arg);

void list_delete(list_t *the_list);
int list_size(list_t *the_list);

#endif

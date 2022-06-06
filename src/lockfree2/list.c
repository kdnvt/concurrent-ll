#include "list.h"
#include <limits.h>
#include <stdalign.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
 

struct back_pack {
    hp_addr_t addr;
    hp_pr_t *pr;
};

struct node {
    int key;
    alignas(8) struct back_pack *back_link;
    alignas(8) struct node *next;
    alignas(8) hp_addr_t next_addr;
};

struct list {
    struct node *head, *tail;
    uint32_t size;
    hp_pr_t *pr;
};

struct two_ptr {
    hp_addr_t cur_addr;
    hp_addr_t next_addr;
};

#define F_MARK 2
#define F_FLAG 1
#define F_MASK 3
#define CUREQ 0
#define NEXTEQ 1

#define MARK(ptr) ((uintptr_t) atomic_load(&(ptr)) | F_MARK)
#define UNMARK(ptr) ((uintptr_t) atomic_load(&(ptr)) & ~F_MARK)
#define ISMARK(ptr) ((uintptr_t) atomic_load(&(ptr)) & F_MARK)

#define FLAG(ptr) ((uintptr_t) atomic_load(&(ptr)) | F_FLAG)
#define UNFLAG(ptr) ((uintptr_t) atomic_load(&(ptr)) & ~F_FLAG)
#define ISFLAG(ptr) ((uintptr_t) atomic_load(&(ptr)) & F_FLAG)

#define ptrof(ptr) ((node_t *) ((uintptr_t) atomic_load(&(ptr)) & ~F_MASK))

hp_addr_t search(list_t *list, int key);

hp_addr_t remove_node(list_t *list, int key, hp_pr_t *pr);

node_t *insert(list_t *list, int key, hp_pr_t *pr);

static struct two_ptr search_from(int key,
                                  hp_addr_t curr_node,
                                  int cur_or_next,
                                  hp_pr_t *pr);

static void help_marked(node_t *prev, node_t *del);

static void help_flag(node_t *prev, node_t *del, hp_pr_t *pr);

static void try_mark(node_t *del, hp_pr_t *pr);

static struct two_ptr try_flag(hp_addr_t prev, node_t *target, hp_pr_t *pr);

static node_t *new_node(int key);
 

hp_pr_t *list_pr(list_t *list){
    return list->pr;
}


/* return true if value already in the list */
bool list_contains(list_t *list, val_t val,hp_t *hp)
{
    hp_addr_t res_addr = search(list, val);
    if (res_addr) {
        hp_pr_release(list->pr, res_addr);
        return true;
    }
    return false;
}
/* insert a new node with the given value val in the list.
 * @return true if succeed
 */
bool list_add(list_t *list, val_t val,hp_t *hp)
{
    if (insert(list, val, list->pr)) {
        return true;
    }
    return false;
}

/* delete a node with the given value val (if the value is present).
 * @return true if succeed
 */
bool list_remove(list_t *list, val_t val,hp_t *hp)
{
     
    hp_addr_t res = remove_node(list, val, list->pr);
    if (res) {
        node_t *res_node = *res;

        hp_pr_release(list->pr, res);
        if (res_node)
            hp_retired(hp, res_node);

         
        return true;
    }
     
    return false;
}

void node_free(void *arg)
{
    node_t *ptr = arg;
    hp_pr_release(ptr->back_link->pr, ptr->back_link->addr);
    hp_pr_release(ptr->back_link->pr, ptr->next_addr);
    free(ptr->back_link);
    free(ptr);
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
    the_list->pr = hp_pr_init();
    return the_list;
}

/* return the node with key if it exists, NULL otherwise. */
hp_addr_t search(list_t *list, int key)
{
    hp_addr_t head_addr = hp_pr_load_mask(list->pr, &list->head, F_MASK);
    struct two_ptr res = search_from(key, head_addr, CUREQ, list->pr);
    hp_pr_release(list->pr, res.next_addr);
    if (((node_t *) (*res.cur_addr))->key == key) {
        return res.cur_addr;
    }
    hp_pr_release(list->pr, res.cur_addr);
    return NULL;
}

/* return true if the node with key is removed successfully.
   return NULL if the node doesn't exist,
   or the node is already removed by others. */
hp_addr_t remove_node(list_t *list, int key, hp_pr_t *pr)
{
    hp_addr_t head_addr = hp_pr_load_mask(pr, &list->head, F_MASK);
    struct two_ptr pos = search_from(key, head_addr, NEXTEQ, pr);

    hp_addr_t del_addr = pos.next_addr;
    node_t *del_node = *del_addr;
    if (del_node->key != key) {
        hp_pr_release(pr, pos.cur_addr);
        hp_pr_release(pr, del_addr);
        return NULL;
    }
    pos = try_flag(pos.cur_addr, del_node,
                   pr);  // only pos.cur_addr should be release
    if (pos.cur_addr) {
        help_flag(*pos.cur_addr, del_node, pr);
        hp_pr_release(pr, pos.cur_addr);
    }
    if (!pos.next_addr) {
        hp_pr_release(pr, del_addr);
        return NULL;
    }
    atomic_fetch_sub(&list->size, 1);
    return del_addr;
}


/*  Return true if the node is inserted in list successfully.
    Return NULL if there is already a node with such key. */
node_t *insert(list_t *list, int key, hp_pr_t *pr)
{
    hp_addr_t head_addr = hp_pr_load_mask(pr, &list->head, F_MASK);
    struct two_ptr pos = search_from(key, head_addr, CUREQ, pr);

    if (((node_t *) (*pos.cur_addr))->key == key) {
        hp_pr_release(pr, pos.cur_addr);
        hp_pr_release(pr, pos.next_addr);
        return NULL;
    }
    node_t *tmp = new_node(key);
    while (1) {
        node_t *prev_succ = atomic_load(&((node_t *) (*pos.cur_addr))->next);
        if (ISFLAG(prev_succ)) {
            help_flag(((node_t *) (*pos.cur_addr)), ptrof(prev_succ), pr);
        } else {
            tmp->next = *pos.next_addr;
            node_t *next_node = *pos.next_addr;
            if (atomic_compare_exchange_strong(
                    &((node_t *) (*pos.cur_addr))->next, &next_node, tmp)) {
                atomic_fetch_add(&list->size, 1);
                hp_pr_release(pr, pos.cur_addr);
                hp_pr_release(pr, pos.next_addr);
                return tmp;
            } else {
                if (next_node ==
                    (node_t *) FLAG(((node_t *) (*pos.cur_addr))->next))
                    help_flag(((node_t *) (*pos.cur_addr)), ptrof(next_node),
                              pr);
                while (ISMARK(((node_t *) (*pos.cur_addr))->next)) {
                    hp_addr_t tmp_addr = pos.cur_addr;
                    pos.cur_addr = hp_pr_load_mask(
                        pr,
                        (void *) ((node_t *) (*pos.cur_addr))->back_link->addr,
                        F_MASK);
                    hp_pr_release(pr, tmp_addr);
                }
            }
        }
        hp_pr_release(pr, pos.next_addr);
        pos = search_from(key, pos.cur_addr, CUREQ, pr);
        if (((node_t *) (*pos.cur_addr))->key == key) {
            hp_pr_release(pr, pos.cur_addr);
            hp_pr_release(pr, pos.next_addr);
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

static struct two_ptr search_from(int key,
                                  hp_addr_t curr_addr,
                                  int cur_or_next,
                                  hp_pr_t *pr)
{
    hp_addr_t next_addr =
        hp_pr_load_mask(pr, &((node_t *) (*curr_addr))->next, F_MASK);
    while ((!cur_or_next && ((node_t *) (*next_addr))->key <= key) ||
           (cur_or_next && ((node_t *) (*next_addr))->key < key)) {
        while (ISMARK(((node_t *) (*next_addr))->next) &&
               (!ISMARK(((node_t *) (*curr_addr))->next) ||
                ptrof(((node_t *) (*curr_addr))->next) !=
                    ((node_t *) (*next_addr)))) {
            if (ptrof(((node_t *) (*curr_addr))->next) ==
                ((node_t *) (*next_addr))) {
                help_marked(((node_t *) (*curr_addr)),
                            ((node_t *) (*next_addr)));
            }
            hp_addr_t tmp = next_addr;
            next_addr =
                hp_pr_load_mask(pr, &((node_t *) (*curr_addr))->next, F_MASK);
            hp_pr_release(pr, tmp);
        }
        if ((!cur_or_next && ((node_t *) (*next_addr))->key <= key) ||
            (cur_or_next && ((node_t *) (*next_addr))->key < key)) {
            hp_addr_t tmp = curr_addr;
            curr_addr = next_addr;

            next_addr = hp_pr_load_mask(pr, &((node_t *) (*next_addr))->next,
                                        F_MASK);  // curr_node -> next_node
            hp_pr_release(pr, tmp);
        }
    }
    return (struct two_ptr){curr_addr, next_addr};
}

static void help_marked(node_t *prev, node_t *del)
{
    hp_addr_t tmp = hp_pr_load_mask(del->back_link->pr, &del->next, F_MASK);
    node_t *next_node = *tmp;
    node_t *exp_del = (node_t *) FLAG(del);
    if (atomic_compare_exchange_strong(&prev->next, &exp_del,
                                       (ptrof(next_node)))) {
        del->next_addr = tmp;
    } else {
        hp_pr_release(ptrof(del)->back_link->pr, tmp);
    }
}

static void help_flag(node_t *prev, node_t *del, hp_pr_t *pr)
{
    struct back_pack *back = malloc(sizeof(*back));
    back->addr = hp_pr_load_mask(pr, &prev, F_MASK);
    back->pr = pr;
    void *exp = NULL;
    if (!atomic_compare_exchange_strong(&del->back_link, &exp, back)) {
        hp_pr_release(pr, back->addr);
        free(back);
    }
    if (!ISMARK(del->next))
        try_mark(del, pr);
    help_marked(prev, del);
}

static void try_mark(node_t *del, hp_pr_t *pr)
{
    do {
        hp_addr_t next_addr = hp_pr_load_mask(pr, &del->next, F_MASK);
        node_t *next_node = *next_addr;
        atomic_compare_exchange_weak(&del->next, &next_node,
                                     (node_t *) MARK(next_node));
        if (ISFLAG(next_node) && *next_addr == ptrof(next_node))
            help_flag(del, *next_addr, pr);
        hp_pr_release(pr, next_addr);
    } while (!ISMARK(del->next));
}

static struct two_ptr try_flag(hp_addr_t prev_addr, node_t *target, hp_pr_t *pr)
{
    node_t *prev = *prev_addr;
    while (1) {
        if (atomic_load(&prev->next) == (node_t *) FLAG(target))
            return (struct two_ptr){prev_addr, NULL};
        node_t *expect = target;
        atomic_compare_exchange_strong(&prev->next, &expect,
                                       (node_t *) FLAG(target));
        if (expect == target)
            return (struct two_ptr){prev_addr, (hp_addr_t) 1};
        if (expect == (node_t *) FLAG(target))
            return (struct two_ptr){prev_addr, NULL};
        while (ISMARK(prev->next)) {
            hp_addr_t tmp = prev_addr;
            prev_addr =
                hp_pr_load_mask(pr, (void *) (prev->back_link->addr), F_MASK);
            hp_pr_release(pr, tmp);
            prev = *prev_addr;
        }
        // search_from will deal with the prev_addr. We only need to deal
        // with the hp_addr_t return to tmp.
        struct two_ptr tmp = search_from(target->key, prev_addr, NEXTEQ, pr);

        if (((node_t *) (*tmp.next_addr)) != target) {
            hp_pr_release(pr, tmp.next_addr);
            hp_pr_release(pr, tmp.cur_addr);
            return (struct two_ptr){NULL, NULL};
        }
        hp_pr_release(pr, tmp.next_addr);
        prev_addr = tmp.cur_addr;
        prev = *prev_addr;
    }
}

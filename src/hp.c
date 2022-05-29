#include "hp.h"
struct _hp_node {
    void *ptr;
    struct _hp_node *next;
};

struct _hp_pr {
    hp_node_t *head;
    int size;
    int list_size;
    void *zero_ptr;
};

struct _hp {
    hp_pr_t *pr;  // The protect list is shared by all threads.

    hp_node_t *retired;  // The retired thread is one each thread.
    int retired_size;    // Therefore, each thread has distinct hp_t,
                         // which has the same protect list but different
                         // retired list.

    void (*dealloc)(void *);
};


static inline hp_addr_t pptr_eq_val(hp_pr_t *pr,
                                    hp_node_t *cur,
                                    void *val,
                                    void **pptr,
                                    uintptr_t mask);

hp_pr_t *hp_pr_init()
{
    hp_pr_t *tmp = malloc(sizeof(*tmp));
    tmp->head = NULL;
    tmp->list_size = 0;
    tmp->size = 0;
    tmp->zero_ptr = NULL;
    return tmp;
}

hp_t *hp_init(hp_pr_t *pr, void (*dealloc)(void *))
{
    hp_t *tmp = malloc(sizeof(*tmp));
    tmp->pr = pr;
    tmp->retired = NULL;
    tmp->retired_size = 0;
    tmp->dealloc = dealloc;
    return tmp;
}

hp_addr_t hp_pr_load(hp_pr_t *pr, void *ptr)
{
    return hp_pr_load_mask(pr, ptr, 0);
}

hp_addr_t hp_pr_load_mask(hp_pr_t *pr, void *ptr, uintptr_t mask)
{
    void **pptr = (void **) ((uintptr_t) ptr);
    void *val;
    if (!(val = (void *) ((uintptr_t) atomic_load(pptr) & ~mask)))
        return (hp_addr_t)(&pr->zero_ptr);
    hp_node_t *cur;
    // try to reuse empty node
    for (cur = atomic_load(&pr->head); cur; cur = cur->next) {
        if (atomic_load(&cur->ptr))
            continue;

        if (!(val = (void *) ((uintptr_t) atomic_load(pptr) & ~mask)))
            return (hp_addr_t)(&pr->zero_ptr);

        void *exp = NULL;
        if (!atomic_compare_exchange_strong(&cur->ptr, &exp, val))
            continue;
        return pptr_eq_val(pr, cur, val, pptr, mask);
    }
    // add new node

    cur = malloc(sizeof(*cur));
    atomic_store(&cur->ptr, NULL);
    if (!(val = (void *) ((uintptr_t) atomic_load(pptr) & ~mask)))
        return (hp_addr_t)(&pr->zero_ptr);
    atomic_store(&cur->ptr, val);
    while (1) {
        cur->next = atomic_load(&pr->head);
        if (atomic_compare_exchange_strong(&pr->head, &cur->next,
                                           cur)) {  // need strong
            atomic_fetch_add(&pr->list_size, 1);
            break;
        }
    }
    return pptr_eq_val(pr, cur, val, pptr, mask);
}

// Check whether *pptr is changed before storing it to hazard pointers.
// If so, try to storing the new *pptr to hazard pointers.
static inline hp_addr_t pptr_eq_val(hp_pr_t *pr,
                                    hp_node_t *cur,
                                    void *val,
                                    void **pptr,
                                    uintptr_t mask)
{
    while (1) {
        if (val == (void *) ((uintptr_t) atomic_load(pptr) & ~mask)) {
            atomic_fetch_add(&pr->size, 1);
            return (hp_addr_t)(&cur->ptr);
        }
        if (!(val = (void *) ((uintptr_t) atomic_load(pptr) & ~mask)))
            return (hp_addr_t)(&pr->zero_ptr);
        atomic_store(&cur->ptr, val);
    }
}

hp_pr_t *hp_get_pr(hp_t *hp)
{
    return hp->pr;
}

void hp_pr_release(hp_pr_t *pr, hp_addr_t ptr_addr)
{
    void **pptr = (void **) ptr_addr;
    if (!(atomic_load(pptr)))  // ptr_addr points to zero_ptr
        return;
    atomic_store(pptr, NULL);
    atomic_fetch_sub(&pr->size, 1);
}

void hp_retired(hp_t *hp, void *ptr)
{
    hp_node_t *tmp = malloc(sizeof(*tmp));
    tmp->next = hp->retired;
    hp->retired = tmp;
    tmp->ptr = ptr;
    hp->retired_size++;

    int threshold = atomic_load(&hp->pr->size);
    threshold += (unsigned) threshold >> 2;
    if (hp->retired_size < threshold)
        return;
    hp_scan(hp);
}

int hp_scan(hp_t *hp)
{
    int count = 0;
    for (hp_node_t **cur_rt = &hp->retired; cur_rt && *cur_rt;) {
        int safe_free = 1;
        for (hp_node_t *cur = atomic_load(&hp->pr->head); cur;
             cur = cur->next) {
            if (atomic_load(&cur->ptr) == (*cur_rt)->ptr) {
                safe_free = 0;
                count++;
                break;
            }
        }

        if (safe_free) {
            hp->dealloc((*cur_rt)->ptr);
            hp_node_t *tmp = *cur_rt;
            *cur_rt = (*cur_rt)->next;
            free(tmp);
            hp->retired_size--;
        } else
            cur_rt = &(*cur_rt)->next;
    }
    return count;
}

int hp_pr_size(hp_pr_t *pr)
{
    return pr->size;
}

void hp_pr_destroy(hp_pr_t *pr)
{
    hp_node_t *tmp;
    while (pr->head) {
        tmp = pr->head;
        pr->head = tmp->next;
        free(tmp);
    }
    free(pr);
}

int get_retired_size(hp_t *hp)
{
    return hp->retired_size;
}
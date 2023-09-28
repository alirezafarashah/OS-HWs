/*
 * mm_alloc.c
 *
 * Stub implementations of the mm_* routines. Remove this comment and provide
 * a summary of your allocator's design here.
 */

#include "mm_alloc.h"
#include <memory.h>
#include <unistd.h>

s_block_ptr head = NULL;

/* Your final implementation should comment out this macro. */

void *mm_malloc(size_t size) {
    if (size == 0)
        return NULL;
    s_block_ptr temp = head;
    s_block_ptr last = NULL;
    while (temp != NULL) {
        if (temp->size >= size) {
            if (temp->free_) {
                temp->free_ = 0;
                split_block(temp, size);
                return temp->ptr;
            }
        }
        last = temp;
        temp = temp->next;
    }
    s_block_ptr res = extend_heap(last, size);
    if (res != NULL) {
        return res->ptr;
    }
    return NULL;
}

void *mm_realloc(void *ptr, size_t size) {
    if (size == 0 && ptr != NULL) {
        mm_free(ptr);
        return NULL;
    }
    if (ptr == NULL) {
        return mm_malloc(size);
    }
    s_block_ptr b = get_block(ptr);
    if (b == NULL) {
        return NULL;
    }
    void *new_ptr = mm_malloc(size);
    memset(new_ptr, 0, size);
    if (size > b->size)
        size = b->size;
    memcpy(new_ptr, ptr, size);
    mm_free(ptr);
    return new_ptr;
}

void mm_free(void *ptr) {
    if (ptr != NULL) {
        s_block_ptr curr_block = get_block(ptr);
        curr_block->free_ = 1;
        fusion(curr_block);

    }
}

s_block_ptr fusion(s_block_ptr b) {
    if (b->prev != NULL && b->prev->free_) {
        (b->prev)->next = b->next;
        if (b->next != NULL) {
            (b->next)->prev = b->prev;
        }
        (b->prev)->size = (b->prev)->size + sizeof(struct s_block) + b->size;
        b = b->prev;
    }
    if (b->next != NULL && b->next->free_) {
        b->size = b->size + sizeof(struct s_block) + (b->next)->size;
        b->next = (b->next)->next;
        if (b->next != NULL) {
            (b->next)->prev = b;
        }
    }
    return b;
}

s_block_ptr get_block(void *p) {
    s_block_ptr temp = head;
    while (temp) {
        if (temp->ptr == p) {
            return temp;
        }
        temp = temp->next;
    }
    return NULL;
}

void split_block(s_block_ptr b, size_t s) {
    if (b != NULL && s > 0) {
        if (b->size - s >= sizeof(struct s_block)) {
            s_block_ptr block = (s_block_ptr) (b->ptr + s);
            block->next = b->next;
            if (block->next != NULL)
                (block->next)->prev = block;
            block->prev = b;
            b->next = block;
            block->size = b->size - s - sizeof(struct s_block);
            b->size = s;
            block->ptr = b->ptr + s + sizeof(struct s_block);
            mm_free(block->ptr);
            memset(b->ptr, 0, b->size);
        }
    }
}

s_block_ptr extend_heap(s_block_ptr last, size_t s) {
    void *brk = sbrk(s + sizeof(struct s_block));
    if (brk != (void *) -1) {
        s_block_ptr new_block = (s_block_ptr) brk;
        new_block->size = s;
        new_block->ptr = brk + sizeof(struct s_block);
        new_block->free_ = 0;
        new_block->next = NULL;
        if (last == NULL) {
            head = new_block;
            new_block->prev = NULL;
            memset(new_block->ptr, 0, new_block->size);
            return new_block;
        }
        last->next = new_block;
        new_block->prev = last;
        memset(new_block->ptr, 0, new_block->size);
        return new_block;
    }
    return NULL;


}
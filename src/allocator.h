#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include "common.h"
#include <stdlib.h>

struct memory_arena
{
    u8* base;
    u64 used;
    u64 size;
};

struct pool_node
{
    struct pool_node* next;
};

struct memory_pool
{
    u8* base;
    struct pool_node* head;
};

inline struct memory_arena allocate_arena(size_t size)
{
    struct memory_arena result = {0};
    result.base = malloc(size);/*VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);*/
    result.size = size;
    return result;
}

inline void free_arena(struct memory_arena* arena)
{
    /*VirtualFree(arena->base, 0, MEM_RELEASE);*/
    free(arena->base);
    arena->base = NULL;
    arena->size = 0;
    arena->used = 0;
}

inline void *push_arena(struct memory_arena* arena, size_t size)
{
    SY_ASSERT(arena->used + size <= arena->size);
    void* memory = arena->base + arena->used;
    arena->used += size;
    return memory;
}

inline void *push_arena_aligned(struct memory_arena *arena, size_t size, size_t alignment)
{
    size_t mask = alignment - 1;
    uintptr_t at = (uintptr_t)(arena->base + arena->used);
    uintptr_t aligned = (at + mask) & ~mask;
    return (void *)aligned;
}

inline struct memory_pool allocate_pool(struct memory_arena *arena, u32 element_size, u32 element_count)
{
    struct memory_pool result = {0};
    result.base = push_arena(arena, (element_count * element_size) + (element_count * sizeof(struct pool_node)));
    u32 block_size = sizeof(struct pool_node) + element_size;
    for (u32 i = 0; i < element_count; ++i)
    {
        struct pool_node *node = (struct pool_node *)(result.base + (block_size * i));
        node->next = result.head;
        result.head = node;
    }
    return result;
}

inline void *pool_alloc(struct memory_pool *pool)
{
    SY_ASSERT(pool->head);
    void* result = pool->head;
    pool->head = pool->head->next;
    return (u8 *)result + sizeof(struct pool_node);
}

inline void pool_dealloc(struct memory_pool* pool, void* base)
{
    struct pool_node *node = (struct pool_node *)((u8 *)base - sizeof(struct pool_node));
    node->next = pool->head;
    pool->head = node;
}

#endif

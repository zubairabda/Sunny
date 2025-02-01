#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <stdlib.h>

#include "common.h"

struct memory_arena
{
    u8 *base;
    u64 used;
    u64 size;
};

struct pool_node
{
    struct pool_node *next;
};

struct memory_pool
{
    u8 *base;
    struct pool_node *head;
    u32 chunk_size;
    u32 chunk_count;
};

inline struct memory_arena allocate_arena(size_t size)
{
    struct memory_arena result = {0};
    result.base = malloc(size);
    result.size = size;
    return result;
}

inline void free_arena(struct memory_arena *arena)
{
    /*VirtualFree(arena->base, 0, MEM_RELEASE);*/
    free(arena->base);
    arena->base = NULL;
    arena->size = 0;
    arena->used = 0;
}

inline void *push_arena(struct memory_arena *arena, size_t size)
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

inline void pool_free_all(struct memory_pool *pool)
{
    for (u32 i = 0; i < pool->chunk_count; ++i)
    {
        struct pool_node *node = (struct pool_node *)(pool->base + pool->chunk_size * i);
        node->next = pool->head;
        pool->head = node;
    }
}

inline struct memory_pool allocate_pool(struct memory_arena *arena, u32 element_size, u32 element_count)
{
    struct memory_pool result = {0};
    // TODO: alignment
    if (element_size < sizeof(struct pool_node)){ 
        element_size = sizeof(struct pool_node);
    }
    result.base = push_arena(arena, element_count * element_size);
    result.chunk_count = element_count;
    result.chunk_size = element_size;
    pool_free_all(&result);
    return result;
}

inline void *pool_alloc(struct memory_pool *pool)
{
    SY_ASSERT(pool->head);
    void *result = pool->head;
    pool->head = pool->head->next;
    return result;
}

inline void pool_dealloc(struct memory_pool *pool, void *base)
{
    struct pool_node *node = base;
    node->next = pool->head;
    pool->head = node;
}

inline u32 fnv1a(const char *text)
{
    const char *p = text;
    char c;
    u32 hash = 2166136261;
    while ((c = *p++))
    {
        hash ^= c;
        hash *= 16777619;
    }
    return hash;
}

inline u32 fnv1a_n(const char *text, u32 length)
{
    u32 hash = 2166136261;
    while (length--)
    {
        char c = *text++;
        hash ^= c;
        hash *= 16777619;
    }
    return hash;
}

inline b8 string_ends_with_ignore_case(const char *str, const char *end)
{
    size_t len = strlen(str);
    size_t end_len = strlen(end);
    if (len >= end_len)
    {
        const char *p = str + (len - end_len);
        while (end_len--)
        {
            char c = *p;
            char d = *end;
            if (c >= 65 && c <= 90)
                c |= 32;
            if (d >= 65 && d <= 90)
                d |= 32;
            if (c != d)
                return 0;
            ++p;
            ++end;
        }
        return 1;
    }
    return 0;
}

#endif /* ALLOCATOR_H */

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

struct memory_arena allocate_arena(u64 size);
void free_arena(struct memory_arena *arena);

struct memory_pool allocate_pool(struct memory_arena *arena, u32 element_size, u32 element_count);
void pool_free_all(struct memory_pool *pool);

b8 string_ends_with_ignore_case(const char *str, const char *end);
b8 string_equals_ignore_case(const char *a, const char *b, u32 len);
s32 string_last_index_of_char(const char *str, char c);

inline void *push_arena(struct memory_arena *arena, u64 size)
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

inline void clear_arena(struct memory_arena *arena)
{
    arena->used = 0;
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

inline u32 fnv1a(const char *str)
{
    const char *p = str;
    char c;
    u32 hash = 2166136261;
    while ((c = *p++))
    {
        hash ^= c;
        hash *= 16777619;
    }
    return hash;
}

inline u32 fnv1a_n(const char *input, u32 length)
{
    u32 hash = 2166136261;
    while (length--)
    {
        char c = *input++;
        hash ^= c;
        hash *= 16777619;
    }
    return hash;
}

inline u32 murmur3_mix32(u32 value)
{
    value ^= value >> 16;
    value *= 0x85ebca6b;
    value ^= value >> 13;
    value *= 0xc2b2ae35;
    value ^= value >> 16;
    return value;
}

inline u64 murmur3_mix64(u64 value)
{
    value ^= value >> 33;
    value *= 0xff51afd7ed558ccdllu;
    value ^= value >> 33;
    value *= 0xc4ceb9fe1a85ec53llu;
    value ^= value >> 33;
    return value;
}

inline void *memset32(void *ptr, u32 value, u32 count)
{
    u32 *dst = ptr;
    while (count--)
        *dst++ = value;
    return ptr;
}

#endif /* ALLOCATOR_H */

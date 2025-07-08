#include "allocator.h"

struct memory_arena allocate_arena(size_t size)
{
    struct memory_arena result = {0};
    result.base = calloc(size, 1);
    result.size = size;
    return result;
}

void free_arena(struct memory_arena *arena)
{
    /*VirtualFree(arena->base, 0, MEM_RELEASE);*/
    free(arena->base);
    arena->base = NULL;
    arena->size = 0;
    arena->used = 0;
}

struct memory_pool allocate_pool(struct memory_arena *arena, u32 element_size, u32 element_count)
{
    struct memory_pool result = {0};
    // TODO: alignment
    if (element_size < sizeof(struct pool_node)) 
        element_size = sizeof(struct pool_node);
        
    result.base = push_arena(arena, element_count * element_size);
    result.chunk_count = element_count;
    result.chunk_size = element_size;
    pool_free_all(&result);
    return result;
}

void pool_free_all(struct memory_pool *pool)
{
    for (u32 i = 0; i < pool->chunk_count; ++i)
    {
        struct pool_node *node = (struct pool_node *)(pool->base + pool->chunk_size * i);
        node->next = pool->head;
        pool->head = node;
    }
}

b8 string_ends_with_ignore_case(const char *str, const char *end)
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

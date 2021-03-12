#include "btrfs_chunk_list.h"

#include <stdlib.h>

#include "debug.h"


struct btrfs_chunk_list {

    struct btrfs_key key;
    struct btrfs_chunk chunk;
    struct btrfs_stripe * more_stripes;

    struct btrfs_chunk_list * next;
};

struct btrfs_chunk_list * btrfs_chunk_list_new(
        struct btrfs_key key,
        struct btrfs_chunk * chunk,
        struct btrfs_chunk_list * next
) {
    struct btrfs_chunk_list * list = malloc(sizeof(struct btrfs_chunk_list));
    u16 num_additional_stripes = btrfs_chunk_num_stripes(chunk) - 1;
    struct btrfs_stripe * stripe;
    u32 i;

    if (!list) {
        return NULL;
    }

    list->key = key;
    list->chunk = *chunk;
    list->next = next;

    if (num_additional_stripes > 1) {
        list->more_stripes = malloc(num_additional_stripes * sizeof(struct btrfs_stripe));

        if (!list->more_stripes) {
            free(list);
            return NULL;
        }

        stripe = (struct btrfs_stripe *) ((u8 *) chunk + sizeof(struct btrfs_chunk));
        for (i = 0; i < num_additional_stripes; ++i) {
            list->more_stripes[i] = *stripe;
            ++stripe;
        }
    }

    return list;
}

void btrfs_chunk_list_delete(struct btrfs_chunk_list * list) {
    struct btrfs_chunk_list * prev;

    while (list) {
        prev = list;
        list = prev->next;

        free(prev->more_stripes);
        free(prev);
    }
}

static void * btrfs_chunk_list_resolve_stripe(
        struct btrfs_chunk_list * list,
        void * data,
        u64 offset
) {
    u64 stripe_len = btrfs_chunk_stripe_len(&list->chunk);

    u64 stripe_number = offset / stripe_len;
    u64 logical_offset = offset - (stripe_number * stripe_len);

    struct btrfs_stripe * stripe;

    if (stripe_number == 0) {
        stripe = &list->chunk.stripe;
    } else {
        stripe = list->more_stripes + (stripe_number - 1);
    }

    return (u8 *) data + btrfs_stripe_offset(stripe) + logical_offset;
}

void * btrfs_chunk_list_resolve(struct btrfs_chunk_list * list, void * data, u64 logical) {
    u64 chunk_offset, logical_chunk_offset;

    for (; list; list = list->next) {
        chunk_offset = list->key.offset;

        if (logical < chunk_offset) {
            continue;
        }

        logical_chunk_offset = logical - chunk_offset;
        if (logical_chunk_offset >= btrfs_chunk_length(&list->chunk)) {
            continue;
        }

        break;
    }

    if (!list) {
        return NULL;
    }

    return btrfs_chunk_list_resolve_stripe(list, data, logical_chunk_offset);
}

#ifdef BTRFS_DEBUG
static inline void btrfs_chunk_list_print_stripe(struct btrfs_stripe * stripe) {
    btrfs_debug_printf("    - %llu:%llu\n", btrfs_stripe_devid(stripe), btrfs_stripe_offset(stripe));
}

void btrfs_chunk_list_print(struct btrfs_chunk_list * list) {
    u16 num_additional_stripes;
    u64 logical, length;
    u32 i;

    btrfs_debug_printf("---- btrfs_chunk_list ----\n");

    for (; list; list = list->next) {
        logical = list->key.offset;
        length = btrfs_chunk_length(&list->chunk);
        num_additional_stripes = btrfs_chunk_num_stripes(&list->chunk) - 1;

        btrfs_debug_printf("  - %llu -> %llu (%llu)\n", logical, logical + length, length);
        btrfs_chunk_list_print_stripe(&list->chunk.stripe);

        for (i = 0; i < num_additional_stripes; ++i) {
            btrfs_chunk_list_print_stripe(list->more_stripes + i);
        }
    }

    btrfs_debug_printf("---- btrfs_chunk_list ----\n");
}
#endif

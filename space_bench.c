/*
 * Copyright (c) 2024 Mehran Toosi
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/time.h>
#include <assert.h>
#include <stddef.h>
#include <inttypes.h>

// RB-Tree implementation for hybrid approach
struct rb_node {
    struct rb_node *rb_parent;
    struct rb_node *rb_right;
    struct rb_node *rb_left;
    int rb_color;
};

struct free_space {
    uint64_t start;
    uint32_t size;
    struct rb_node node;
};

#define RB_RED   0
#define RB_BLACK 1

static struct rb_node *rb_root = NULL;

// Traditional approach using linked list
struct list_node {
    uint64_t start;
    uint32_t size;
    struct list_node *next;
};

static struct list_node *list_head = NULL;

// Benchmark parameters
#define TOTAL_SPACE (1ULL << 30)  // 1GB
#define MIN_ALLOC   (1 << 12)     // 4KB
#define MAX_ALLOC   (1 << 20)     // 1MB
#define NUM_OPS     100000        // 100K operations
#define ALLOC_PROB  0.7          // 70% allocations, 30% frees
#define MIN_USEFUL_SIZE (1 << 12) // 4KB minimum useful size
#define FRAGMENTATION_THRESHOLD (128 * 1024)  // 128KB threshold for fragmentation

// Timing utilities
static inline uint64_t get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000ULL + tv.tv_usec;
}

// RB-Tree operations
static void rb_link_node(struct rb_node *node, struct rb_node *parent, struct rb_node **rb_link) {
    node->rb_parent = parent;
    node->rb_color = RB_RED;
    node->rb_left = node->rb_right = NULL;
    *rb_link = node;
}

static void rb_rotate_left(struct rb_node *node, struct rb_node **root) {
    struct rb_node *right = node->rb_right;
    struct rb_node *parent = node->rb_parent;

    if ((node->rb_right = right->rb_left))
        right->rb_left->rb_parent = node;
    right->rb_left = node;

    right->rb_parent = parent;
    if (parent) {
        if (node == parent->rb_left)
            parent->rb_left = right;
        else
            parent->rb_right = right;
    } else {
        *root = right;
    }
    node->rb_parent = right;
}

static void rb_rotate_right(struct rb_node *node, struct rb_node **root) {
    struct rb_node *left = node->rb_left;
    struct rb_node *parent = node->rb_parent;

    if ((node->rb_left = left->rb_right))
        left->rb_right->rb_parent = node;
    left->rb_right = node;

    left->rb_parent = parent;
    if (parent) {
        if (node == parent->rb_right)
            parent->rb_right = left;
        else
            parent->rb_left = left;
    } else {
        *root = left;
    }
    node->rb_parent = left;
}

static void rb_insert_color(struct rb_node *node, struct rb_node **root) {
    struct rb_node *parent, *gparent;

    while ((parent = node->rb_parent) && parent->rb_color == RB_RED) {
        gparent = parent->rb_parent;
        if (!gparent)
            break;

        if (parent == gparent->rb_left) {
            struct rb_node *uncle = gparent->rb_right;
            if (uncle && uncle->rb_color == RB_RED) {
                uncle->rb_color = RB_BLACK;
                parent->rb_color = RB_BLACK;
                gparent->rb_color = RB_RED;
                node = gparent;
                continue;
            }

            if (parent->rb_right == node) {
                rb_rotate_left(parent, root);
                struct rb_node *tmp = parent;
                parent = node;
                node = tmp;
            }

            parent->rb_color = RB_BLACK;
            gparent->rb_color = RB_RED;
            rb_rotate_right(gparent, root);
        } else {
            struct rb_node *uncle = gparent->rb_left;
            if (uncle && uncle->rb_color == RB_RED) {
                uncle->rb_color = RB_BLACK;
                parent->rb_color = RB_BLACK;
                gparent->rb_color = RB_RED;
                node = gparent;
                continue;
            }

            if (parent->rb_left == node) {
                rb_rotate_right(parent, root);
                struct rb_node *tmp = parent;
                parent = node;
                node = tmp;
            }

            parent->rb_color = RB_BLACK;
            gparent->rb_color = RB_RED;
            rb_rotate_left(gparent, root);
        }
    }

    (*root)->rb_color = RB_BLACK;
}

static void rb_erase_color(struct rb_node *node, struct rb_node *parent, struct rb_node **root) {
    struct rb_node *other;

    while ((!node || node->rb_color == RB_BLACK) && node != *root) {
        if (parent->rb_left == node) {
            other = parent->rb_right;
            if (other->rb_color == RB_RED) {
                other->rb_color = RB_BLACK;
                parent->rb_color = RB_RED;
                rb_rotate_left(parent, root);
                other = parent->rb_right;
            }
            if ((!other->rb_left || other->rb_left->rb_color == RB_BLACK) &&
                (!other->rb_right || other->rb_right->rb_color == RB_BLACK)) {
                other->rb_color = RB_RED;
                node = parent;
                parent = node->rb_parent;
            } else {
                if (!other->rb_right || other->rb_right->rb_color == RB_BLACK) {
                    if (other->rb_left)
                        other->rb_left->rb_color = RB_BLACK;
                    other->rb_color = RB_RED;
                    rb_rotate_right(other, root);
                    other = parent->rb_right;
                }
                other->rb_color = parent->rb_color;
                parent->rb_color = RB_BLACK;
                if (other->rb_right)
                    other->rb_right->rb_color = RB_BLACK;
                rb_rotate_left(parent, root);
                node = *root;
                break;
            }
        } else {
            other = parent->rb_left;
            if (other->rb_color == RB_RED) {
                other->rb_color = RB_BLACK;
                parent->rb_color = RB_RED;
                rb_rotate_right(parent, root);
                other = parent->rb_left;
            }
            if ((!other->rb_right || other->rb_right->rb_color == RB_BLACK) &&
                (!other->rb_left || other->rb_left->rb_color == RB_BLACK)) {
                other->rb_color = RB_RED;
                node = parent;
                parent = node->rb_parent;
            } else {
                if (!other->rb_left || other->rb_left->rb_color == RB_BLACK) {
                    if (other->rb_right)
                        other->rb_right->rb_color = RB_BLACK;
                    other->rb_color = RB_RED;
                    rb_rotate_left(other, root);
                    other = parent->rb_left;
                }
                other->rb_color = parent->rb_color;
                parent->rb_color = RB_BLACK;
                if (other->rb_left)
                    other->rb_left->rb_color = RB_BLACK;
                rb_rotate_right(parent, root);
                node = *root;
                break;
            }
        }
    }
    if (node)
        node->rb_color = RB_BLACK;
}

static void rb_erase(struct rb_node *node, struct rb_node **root) {
    struct rb_node *child, *parent;
    int color;

    if (!node->rb_left)
        child = node->rb_right;
    else if (!node->rb_right)
        child = node->rb_left;
    else {
        struct rb_node *old = node, *left;
        node = node->rb_right;
        while ((left = node->rb_left))
            node = left;
        child = node->rb_right;
        parent = node->rb_parent;
        color = node->rb_color;

        if (child)
            child->rb_parent = parent;
        if (parent) {
            if (parent->rb_left == node)
                parent->rb_left = child;
            else
                parent->rb_right = child;
        } else
            *root = child;

        if (node->rb_parent == old)
            parent = node;

        node->rb_parent = old->rb_parent;
        node->rb_color = old->rb_color;
        node->rb_right = old->rb_right;
        node->rb_left = old->rb_left;

        if (old->rb_parent) {
            if (old->rb_parent->rb_left == old)
                old->rb_parent->rb_left = node;
            else
                old->rb_parent->rb_right = node;
        } else
            *root = node;

        old->rb_left->rb_parent = node;
        if (old->rb_right)
            old->rb_right->rb_parent = node;
        goto color_fixup;
    }

    parent = node->rb_parent;
    color = node->rb_color;

    if (child)
        child->rb_parent = parent;
    if (parent) {
        if (parent->rb_left == node)
            parent->rb_left = child;
        else
            parent->rb_right = child;
    } else
        *root = child;

color_fixup:
    if (color == RB_BLACK)
        rb_erase_color(child, parent, root);
}

// Forward declarations
static struct rb_node *rb_next(struct rb_node *node);
static struct free_space *find_space_by_start(uint64_t start);
static struct free_space *try_merge_with_adjacent(struct free_space *space);
static void hybrid_remove_space(struct free_space *space);
static void hybrid_insert_space(struct free_space *space);

#define BLOCKS_PER_GROUP (128 * 1024)  // 128K blocks per group
#define GROUP_COUNT ((TOTAL_SPACE / MIN_ALLOC + BLOCKS_PER_GROUP - 1) / BLOCKS_PER_GROUP)

// Hybrid approach with block groups
struct hybrid_group {
    struct rb_node *rb_root;
    uint32_t free_blocks;      // Total free blocks in group
    uint32_t largest_free;     // Largest contiguous space
    uint32_t last_alloc_size;  // Size of last successful allocation
    uint64_t last_alloc_start; // Start of last successful allocation
};

struct hybrid_allocator {
    struct hybrid_group *groups;
    uint32_t groups_count;
    uint32_t blocks_per_group;
    uint32_t total_blocks;
    uint32_t last_group_blocks;
    uint32_t last_alloc_group; // Cache last successful group
};

static struct hybrid_allocator hybrid;

// Initialize hybrid approach with block groups
static void hybrid_init(void) {
    hybrid.groups_count = GROUP_COUNT;
    hybrid.blocks_per_group = BLOCKS_PER_GROUP;
    hybrid.total_blocks = TOTAL_SPACE / MIN_ALLOC;
    hybrid.last_group_blocks = hybrid.total_blocks % BLOCKS_PER_GROUP;
    if (hybrid.last_group_blocks == 0) {
        hybrid.last_group_blocks = BLOCKS_PER_GROUP;
    }
    hybrid.last_alloc_group = 0;
    
    hybrid.groups = calloc(hybrid.groups_count, sizeof(struct hybrid_group));
    for (uint32_t i = 0; i < hybrid.groups_count; i++) {
        uint32_t group_blocks = (i == hybrid.groups_count - 1) ? 
                              hybrid.last_group_blocks : BLOCKS_PER_GROUP;
        
        hybrid.groups[i].rb_root = NULL;
        hybrid.groups[i].free_blocks = group_blocks;
        hybrid.groups[i].largest_free = group_blocks;
        hybrid.groups[i].last_alloc_size = 0;
        hybrid.groups[i].last_alloc_start = 0;
        
        // Initialize with one large free space
        struct free_space *initial = malloc(sizeof(*initial));
        initial->start = i * BLOCKS_PER_GROUP;
        initial->size = group_blocks;
        hybrid_insert_space(initial);
    }
}

// Find space in hybrid allocator
static struct free_space *hybrid_find_space(uint32_t blocks_needed) {
    // First try the last successful group
    struct hybrid_group *last_group = &hybrid.groups[hybrid.last_alloc_group];
    
    // If last allocation was similar size, try the same area
    if (last_group->last_alloc_size >= blocks_needed && 
        last_group->last_alloc_size <= blocks_needed * 2) {
        struct free_space *space = find_space_by_start(last_group->last_alloc_start);
        if (space && space->size >= blocks_needed) {
            return space;
        }
    }
    
    // Try groups with enough free space
    for (uint32_t g = 0; g < hybrid.groups_count; g++) {
        struct hybrid_group *group = &hybrid.groups[g];
        if (group->largest_free >= blocks_needed) {
            struct rb_node *node = group->rb_root;
            struct free_space *best_fit = NULL;
            
            while (node) {
                struct free_space *space = (struct free_space *)((char *)node - offsetof(struct free_space, node));
                if (space->size >= blocks_needed) {
                    best_fit = space;
                    if (space->size <= blocks_needed * 2) { // Good enough fit
                        break;
                    }
                    node = node->rb_left;
                } else {
                    node = node->rb_right;
                }
            }
            
            if (best_fit) {
                hybrid.last_alloc_group = g;
                group->last_alloc_size = blocks_needed;
                group->last_alloc_start = best_fit->start;
                return best_fit;
            }
        }
    }
    
    return NULL;
}

// Insert space into hybrid allocator
static void hybrid_insert_space(struct free_space *space) {
    uint32_t group_index = space->start / hybrid.blocks_per_group;
    struct hybrid_group *group = &hybrid.groups[group_index];
    
    // Try to merge with adjacent spaces first
    struct free_space *merged = try_merge_with_adjacent(space);
    
    // Update group statistics
    group->free_blocks += merged->size;
    if (merged->size > group->largest_free) {
        group->largest_free = merged->size;
    }
    
    // If this is a new space (not merged into an existing one)
    if (merged == space) {
        struct rb_node **new = &group->rb_root;
        struct rb_node *parent = NULL;
        
        while (*new) {
            struct free_space *this = (struct free_space *)((char *)*new - offsetof(struct free_space, node));
            parent = *new;
            
            if (space->start < this->start)
                new = &((*new)->rb_left);
            else
                new = &((*new)->rb_right);
        }
        
        rb_link_node(&space->node, parent, new);
        rb_insert_color(&space->node, &group->rb_root);
    }
}

// Remove space from hybrid allocator
static void hybrid_remove_space(struct free_space *space) {
    uint32_t group_index = space->start / hybrid.blocks_per_group;
    struct hybrid_group *group = &hybrid.groups[group_index];
    
    group->free_blocks -= space->size;
    if (space->size == group->largest_free) {
        // Need to find new largest free space
        group->largest_free = 0;
        struct rb_node *node;
        for (node = group->rb_root; node; node = rb_next(node)) {
            struct free_space *curr = (struct free_space *)((char *)node - offsetof(struct free_space, node));
            if (curr->size > group->largest_free) {
                group->largest_free = curr->size;
            }
        }
    }
    
    rb_erase(&space->node, &group->rb_root);
}

// RB-Tree traversal
static struct rb_node *rb_next(struct rb_node *node) {
    struct rb_node *parent;

    if (!node)
        return NULL;

    /* If we have a right-hand child, go down and then left as far
       as we can. */
    if (node->rb_right) {
        node = node->rb_right;
        while (node->rb_left)
            node = node->rb_left;
        return node;
    }

    /* No right-hand children. Go up till we find an ancestor which
       is a left-hand child of its parent */
    while ((parent = node->rb_parent) && node == parent->rb_right)
        node = parent;

    return parent;
}

// Add function to find space by start sector
static struct free_space *find_space_by_start(uint64_t start) {
    uint32_t group_index = start / hybrid.blocks_per_group;
    struct hybrid_group *group = &hybrid.groups[group_index];
    struct rb_node *node = group->rb_root;
    
    while (node) {
        struct free_space *space = (struct free_space *)((char *)node - offsetof(struct free_space, node));
        if (space->start == start)
            return space;
        if (start < space->start)
            node = node->rb_left;
        else
            node = node->rb_right;
    }
    return NULL;
}

// Add function to try merging with adjacent spaces
static struct free_space *try_merge_with_adjacent(struct free_space *space) {
    struct free_space *merged = space;
    uint64_t end = space->start + space->size;
    uint32_t group_index = space->start / hybrid.blocks_per_group;
    uint32_t end_group_index = end / hybrid.blocks_per_group;
    
    // Only merge within the same group
    if (group_index == end_group_index) {
        // Try to find and merge with space to the right
        struct free_space *right = find_space_by_start(end);
        if (right) {
            merged->size += right->size;
            hybrid_remove_space(right);
            free(right);
        }
        
        // Try to find space to the left
        struct hybrid_group *group = &hybrid.groups[group_index];
        struct rb_node *node = group->rb_root;
        struct free_space *left = NULL;
        
        while (node) {
            struct free_space *curr = (struct free_space *)((char *)node - offsetof(struct free_space, node));
            if (curr->start + curr->size == space->start) {
                left = curr;
                break;
            }
            if (curr->start < space->start)
                node = node->rb_right;
            else
                node = node->rb_left;
        }
        
        if (left) {
            left->size += merged->size;
            if (merged != space) {
                hybrid_remove_space(merged);
                free(merged);
            }
            merged = left;
        }
    }
    
    return merged;
}

// Calculate fragmentation for hybrid approach
static double calculate_fragmentation_hybrid(void) {
    uint64_t total_free = 0;
    uint64_t fragmented = 0;
    uint64_t max_contiguous = 0;
    uint64_t num_free_runs = 0;
    uint32_t threshold_blocks = FRAGMENTATION_THRESHOLD / MIN_ALLOC;
    
    for (uint32_t g = 0; g < hybrid.groups_count; g++) {
        struct hybrid_group *group = &hybrid.groups[g];
        struct rb_node *node;
        
        for (node = group->rb_root; node; node = rb_next(node)) {
            struct free_space *space = (struct free_space *)((char *)node - offsetof(struct free_space, node));
            total_free += space->size;
            num_free_runs++;
            
            if (space->size > max_contiguous) {
                max_contiguous = space->size;
            }
            
            // Consider a space fragmented if it can't fit a medium allocation (128KB)
            if (space->size < threshold_blocks) {
                fragmented += space->size;
            }
        }
    }

    // Print additional fragmentation metrics
    printf("Largest contiguous free space: %" PRIu64 " blocks (%" PRIu64 " bytes)\n", 
           max_contiguous, max_contiguous * MIN_ALLOC);
    printf("Total free space: %" PRIu64 " blocks (%" PRIu64 " bytes)\n", 
           total_free, total_free * MIN_ALLOC);
    printf("Total fragmented space: %" PRIu64 " blocks (%" PRIu64 " bytes)\n", 
           fragmented, fragmented * MIN_ALLOC);
    printf("Number of free runs: %" PRIu64 "\n", num_free_runs);
    if (num_free_runs > 0) {
        printf("Average free run size: %" PRIu64 " blocks\n", total_free / num_free_runs);
    }
    
    return total_free ? ((double)fragmented * 100.0) / total_free : 0.0;
}

// Benchmark functions
static void run_hybrid_benchmark(void) {
    printf("\nHybrid Approach Results:\n");
    fflush(stdout);

    hybrid_init();

    uint64_t start_time = get_time_us();
    uint64_t total_alloc_time = 0;
    uint64_t total_free_time = 0;
    int alloc_count = 0;
    int free_count = 0;

    // Array to track allocations for freeing
    struct free_space **allocations = malloc(NUM_OPS * sizeof(struct free_space *));
    int alloc_index = 0;

    for (int i = 0; i < NUM_OPS; i++) {
        if ((double)rand() / RAND_MAX < ALLOC_PROB && alloc_index < NUM_OPS) {
            // Allocation
            uint32_t size = MIN_ALLOC + rand() % (MAX_ALLOC - MIN_ALLOC);
            uint32_t blocks = (size + MIN_ALLOC - 1) / MIN_ALLOC;
            
            uint64_t before = get_time_us();
            struct free_space *space = hybrid_find_space(blocks);
            if (space) {
                // Track allocation
                struct free_space *alloc = malloc(sizeof(*alloc));
                alloc->start = space->start;
                alloc->size = blocks;
                allocations[alloc_index++] = alloc;

                // Update or remove the free space
                if (space->size > blocks) {
                    space->start += blocks;
                    space->size -= blocks;
                } else {
                    hybrid_remove_space(space);
                    free(space);
                }
                total_alloc_time += get_time_us() - before;
                alloc_count++;
            }
        } else if (alloc_index > 0) {
            // Free
            int index = rand() % alloc_index;
            struct free_space *space = allocations[index];
            
            uint64_t before = get_time_us();
            hybrid_insert_space(space);
            total_free_time += get_time_us() - before;
            
            // Remove from tracking array
            allocations[index] = allocations[--alloc_index];
            free_count++;
        }
    }

    uint64_t total_time = get_time_us() - start_time;
    printf("Total time: %" PRIu64 " us\n", total_time);
    printf("Average allocation time: %" PRIu64 " us\n", alloc_count ? total_alloc_time / alloc_count : 0);
    printf("Average free time: %" PRIu64 " us\n", free_count ? total_free_time / free_count : 0);
    printf("Operations per second: %.2f\n", (NUM_OPS * 1000000.0) / total_time);

    double fragmentation = calculate_fragmentation_hybrid();
    printf("Fragmentation: %.2f%%\n", fragmentation);

    // Cleanup
    for (uint32_t i = 0; i < hybrid.groups_count; i++) {
        struct rb_node *node = hybrid.groups[i].rb_root;
        while (node) {
            struct free_space *space = (struct free_space *)((char *)node - offsetof(struct free_space, node));
            node = rb_next(node);
            hybrid_remove_space(space);
            free(space);
        }
    }
    free(hybrid.groups);
    free(allocations);
}

#define BITS_PER_LONG 64
#define BITMAP_SIZE ((TOTAL_SPACE / MIN_ALLOC + BITS_PER_LONG - 1) / BITS_PER_LONG)

static unsigned long *bitmap;
static size_t total_bits;

static void bitmap_init(void) {
    total_bits = TOTAL_SPACE / MIN_ALLOC;
    bitmap = calloc(BITMAP_SIZE, sizeof(unsigned long));
    // Initially all space is free (bits set to 1)
    memset(bitmap, 0xFF, BITMAP_SIZE * sizeof(unsigned long));
}

static void bitmap_cleanup(void) {
    free(bitmap);
}

static inline int test_bit(int nr, const unsigned long *addr) {
    return ((1UL << (nr % BITS_PER_LONG)) & 
            (addr[nr / BITS_PER_LONG])) != 0;
}

static inline void clear_bit(int nr, unsigned long *addr) {
    addr[nr / BITS_PER_LONG] &= ~(1UL << (nr % BITS_PER_LONG));
}

static inline void set_bit(int nr, unsigned long *addr) {
    addr[nr / BITS_PER_LONG] |= (1UL << (nr % BITS_PER_LONG));
}

// Find first sequence of n free blocks
static int bitmap_find_free_space(uint32_t blocks_needed) {
    uint32_t count = 0;
    int start = -1;

    for (size_t i = 0; i < total_bits; i++) {
        if (test_bit(i, bitmap)) {
            if (start == -1) start = i;
            count++;
            if (count == blocks_needed) return start;
        } else {
            start = -1;
            count = 0;
        }
    }
    return -1;
}

static void bitmap_allocate_range(int start, uint32_t blocks) {
    for (uint32_t i = 0; i < blocks; i++) {
        clear_bit(start + i, bitmap);
    }
}

static void bitmap_free_range(int start, uint32_t blocks) {
    for (uint32_t i = 0; i < blocks; i++) {
        set_bit(start + i, bitmap);
    }
}

static double calculate_bitmap_fragmentation(void) {
    uint64_t total_free = 0;
    uint64_t fragmented = 0;
    uint64_t current_run = 0;
    uint64_t max_contiguous = 0;
    uint64_t num_free_runs = 0;
    uint32_t threshold_blocks = FRAGMENTATION_THRESHOLD / MIN_ALLOC;

    // First pass: count total free space and largest contiguous block
    for (size_t i = 0; i <= total_bits; i++) {
        if (i < total_bits && test_bit(i, bitmap)) {
            current_run++;
            total_free++;
        } else if (current_run > 0) {
            // We found the end of a free run
            num_free_runs++;
            if (current_run > max_contiguous) {
                max_contiguous = current_run;
            }
            // Consider a space fragmented if it can't fit a medium allocation (128KB)
            if (current_run < threshold_blocks) {
                fragmented += current_run;
            }
            current_run = 0;
        }
    }

    // Print additional fragmentation metrics
    printf("Largest contiguous free space: %" PRIu64 " blocks (%" PRIu64 " bytes)\n", 
           max_contiguous, max_contiguous * MIN_ALLOC);
    printf("Total free space: %" PRIu64 " blocks (%" PRIu64 " bytes)\n", 
           total_free, total_free * MIN_ALLOC);
    printf("Total fragmented space: %" PRIu64 " blocks (%" PRIu64 " bytes)\n", 
           fragmented, fragmented * MIN_ALLOC);
    printf("Number of free runs: %" PRIu64 "\n", num_free_runs);
    if (num_free_runs > 0) {
        printf("Average free run size: %" PRIu64 " blocks\n", total_free / num_free_runs);
    }

    return total_free ? ((double)fragmented * 100.0) / total_free : 0.0;
}

static void run_bitmap_benchmark(void) {
    printf("\nBitmap Approach Results:\n");
    fflush(stdout);

    bitmap_init();

    uint64_t start_time = get_time_us();
    uint64_t total_alloc_time = 0;
    uint64_t total_free_time = 0;
    int alloc_count = 0;
    int free_count = 0;

    // Array to track allocations for freeing
    struct {
        int start;
        uint32_t blocks;
    } *allocations = malloc(NUM_OPS * sizeof(*allocations));
    int alloc_index = 0;

    for (int i = 0; i < NUM_OPS; i++) {
        if ((double)rand() / RAND_MAX < ALLOC_PROB && alloc_index < NUM_OPS) {
            // Allocation
            uint32_t size = MIN_ALLOC + rand() % (MAX_ALLOC - MIN_ALLOC);
            uint32_t blocks_needed = (size + MIN_ALLOC - 1) / MIN_ALLOC;
            
            uint64_t before = get_time_us();
            int start = bitmap_find_free_space(blocks_needed);
            if (start >= 0) {
                bitmap_allocate_range(start, blocks_needed);
                allocations[alloc_index].start = start;
                allocations[alloc_index].blocks = blocks_needed;
                alloc_index++;
                total_alloc_time += get_time_us() - before;
                alloc_count++;
            }
        } else if (alloc_index > 0) {
            // Free
            int index = rand() % alloc_index;
            uint64_t before = get_time_us();
            bitmap_free_range(allocations[index].start, allocations[index].blocks);
            total_free_time += get_time_us() - before;
            
            // Remove from tracking array
            allocations[index] = allocations[--alloc_index];
            free_count++;
        }
    }

    uint64_t total_time = get_time_us() - start_time;
    printf("Total time: %" PRIu64 " us\n", total_time);
    printf("Average allocation time: %" PRIu64 " us\n", 
           alloc_count ? total_alloc_time / alloc_count : 0);
    printf("Average free time: %" PRIu64 " us\n", 
           free_count ? total_free_time / free_count : 0);
    printf("Operations per second: %.2f\n", (NUM_OPS * 1000000.0) / total_time);

    double fragmentation = calculate_bitmap_fragmentation();
    printf("Fragmentation ratio: %.2f%%\n", fragmentation);

    free(allocations);
    bitmap_cleanup();
}

#define BLOCKS_PER_GROUP (128 * 1024)  // 128K blocks per group
#define GROUP_COUNT ((TOTAL_SPACE / MIN_ALLOC + BLOCKS_PER_GROUP - 1) / BLOCKS_PER_GROUP)

// Optimized bitmap with block groups
struct block_group {
    unsigned long *bitmap;
    uint32_t free_blocks;      // Pre-computed count
    uint32_t first_free_hint;  // Hint for next search
    uint32_t largest_free;     // Largest contiguous space
    uint32_t last_update;      // Last update counter
    bool needs_update;         // Whether stats need update
};

struct optimized_bitmap {
    struct block_group *groups;
    uint32_t groups_count;
    uint32_t blocks_per_group;
    uint32_t total_blocks;
    uint32_t last_group_blocks;
    uint32_t update_counter;   // Global counter for lazy updates
    uint32_t last_alloc_group; // Cache last successful group
};

static struct optimized_bitmap opt_bitmap;

// Efficient bit operations
static inline unsigned long _find_next_zero_bit(const unsigned long *addr,
                                              unsigned long size,
                                              unsigned long offset) {
    const unsigned long *p = addr + (offset / BITS_PER_LONG);
    unsigned long result = offset & ~(BITS_PER_LONG - 1);
    unsigned long tmp;

    if (offset >= size)
        return size;

    size -= result;
    offset %= BITS_PER_LONG;
    if (offset) {
        tmp = *(p++);
        tmp |= ~0UL >> (BITS_PER_LONG - offset);
        if (size < BITS_PER_LONG)
            goto found_first;
        if (~tmp)
            goto found_middle;
        size -= BITS_PER_LONG;
        result += BITS_PER_LONG;
    }
    while (size & ~(BITS_PER_LONG - 1)) {
        if (~(tmp = *(p++)))
            goto found_middle;
        result += BITS_PER_LONG;
        size -= BITS_PER_LONG;
    }
    if (!size)
        return result;
    tmp = *p;

found_first:
    tmp |= ~0UL << size;
    if (tmp == ~0UL)
        return result + size;
found_middle:
    return result + __builtin_ffsl(~tmp) - 1;
}

static inline unsigned long _find_next_bit(const unsigned long *addr,
                                         unsigned long size,
                                         unsigned long offset) {
    const unsigned long *p = addr + (offset / BITS_PER_LONG);
    unsigned long result = offset & ~(BITS_PER_LONG - 1);
    unsigned long tmp;

    if (offset >= size)
        return size;

    size -= result;
    offset %= BITS_PER_LONG;
    if (offset) {
        tmp = *(p++);
        tmp &= ~0UL << offset;
        if (size < BITS_PER_LONG)
            goto found_first;
        if (tmp)
            goto found_middle;
        size -= BITS_PER_LONG;
        result += BITS_PER_LONG;
    }
    while (size & ~(BITS_PER_LONG - 1)) {
        if ((tmp = *(p++)))
            goto found_middle;
        result += BITS_PER_LONG;
        size -= BITS_PER_LONG;
    }
    if (!size)
        return result;
    tmp = *p;

found_first:
    tmp &= ~0UL >> (BITS_PER_LONG - size);
    if (!tmp)
        return result + size;
found_middle:
    return result + __builtin_ffsl(tmp) - 1;
}

// Initialize optimized bitmap
static void opt_bitmap_init(void) {
    opt_bitmap.groups_count = GROUP_COUNT;
    opt_bitmap.blocks_per_group = BLOCKS_PER_GROUP;
    opt_bitmap.total_blocks = TOTAL_SPACE / MIN_ALLOC;
    opt_bitmap.last_group_blocks = opt_bitmap.total_blocks % BLOCKS_PER_GROUP;
    if (opt_bitmap.last_group_blocks == 0) {
        opt_bitmap.last_group_blocks = BLOCKS_PER_GROUP;
    }
    opt_bitmap.update_counter = 0;
    opt_bitmap.last_alloc_group = 0;
    
    opt_bitmap.groups = calloc(opt_bitmap.groups_count, sizeof(struct block_group));
    for (uint32_t i = 0; i < opt_bitmap.groups_count; i++) {
        uint32_t group_blocks = (i == opt_bitmap.groups_count - 1) ? 
                              opt_bitmap.last_group_blocks : BLOCKS_PER_GROUP;
        uint32_t bitmap_longs = (group_blocks + BITS_PER_LONG - 1) / BITS_PER_LONG;
        
        opt_bitmap.groups[i].bitmap = calloc(bitmap_longs, sizeof(unsigned long));
        memset(opt_bitmap.groups[i].bitmap, 0xFF, bitmap_longs * sizeof(unsigned long));
        opt_bitmap.groups[i].free_blocks = group_blocks;
        opt_bitmap.groups[i].first_free_hint = 0;
        opt_bitmap.groups[i].largest_free = group_blocks;
        opt_bitmap.groups[i].last_update = 0;
        opt_bitmap.groups[i].needs_update = false;
    }
}

static void opt_bitmap_cleanup(void) {
    for (uint32_t i = 0; i < opt_bitmap.groups_count; i++) {
        free(opt_bitmap.groups[i].bitmap);
    }
    free(opt_bitmap.groups);
}

// Lazy group stats update
static void update_group_stats(struct block_group *group, uint32_t group_blocks) {
    if (!group->needs_update) return;

    uint32_t max_free = 0;
    uint32_t free_blocks = 0;
    uint32_t first_free = group_blocks;

    for (uint32_t i = 0; i < group_blocks;) {
        unsigned long j = _find_next_bit(group->bitmap, group_blocks, i);
        if (j >= group_blocks) break;
        
        unsigned long k = _find_next_zero_bit(group->bitmap, group_blocks, j);
        uint32_t run = k - j;
        
        free_blocks += run;
        if (run > max_free) max_free = run;
        if (j < first_free) first_free = j;
        
        i = k;
    }

    group->free_blocks = free_blocks;
    group->largest_free = max_free;
    group->first_free_hint = first_free;
    group->needs_update = false;
    group->last_update = opt_bitmap.update_counter;
}

// Optimized space finding
static int opt_bitmap_find_free_space(uint32_t blocks_needed) {
    // First try the last successful group
    struct block_group *last_group = &opt_bitmap.groups[opt_bitmap.last_alloc_group];
    uint32_t last_blocks = (opt_bitmap.last_alloc_group == opt_bitmap.groups_count - 1) ? 
                          opt_bitmap.last_group_blocks : opt_bitmap.blocks_per_group;
    
    if (last_group->needs_update || 
        last_group->last_update + 100 < opt_bitmap.update_counter) {
        update_group_stats(last_group, last_blocks);
    }

    if (last_group->largest_free >= blocks_needed) {
        uint32_t base = opt_bitmap.last_alloc_group * opt_bitmap.blocks_per_group;
        for (uint32_t i = last_group->first_free_hint; i < last_blocks;) {
            unsigned long j = _find_next_bit(last_group->bitmap, last_blocks, i);
            if (j >= last_blocks) break;
            
            unsigned long k = _find_next_zero_bit(last_group->bitmap, last_blocks, j);
            uint32_t run = k - j;
            
            if (run >= blocks_needed) {
                return base + j;
            }
            i = k;
        }
    }

    // Search other groups
    for (uint32_t g = 0; g < opt_bitmap.groups_count; g++) {
        if (g == opt_bitmap.last_alloc_group) continue;
        
        struct block_group *group = &opt_bitmap.groups[g];
        uint32_t group_blocks = (g == opt_bitmap.groups_count - 1) ? 
                              opt_bitmap.last_group_blocks : opt_bitmap.blocks_per_group;

        if (group->needs_update || 
            group->last_update + 100 < opt_bitmap.update_counter) {
            update_group_stats(group, group_blocks);
        }

        if (group->largest_free >= blocks_needed) {
            uint32_t base = g * opt_bitmap.blocks_per_group;
            for (uint32_t i = group->first_free_hint; i < group_blocks;) {
                unsigned long j = _find_next_bit(group->bitmap, group_blocks, i);
                if (j >= group_blocks) break;
                
                unsigned long k = _find_next_zero_bit(group->bitmap, group_blocks, j);
                uint32_t run = k - j;
                
                if (run >= blocks_needed) {
                    opt_bitmap.last_alloc_group = g;
                    return base + j;
                }
                i = k;
            }
        }
    }
    
    return -1;
}

// Optimized allocation
static void opt_bitmap_allocate_range(int start, uint32_t blocks) {
    uint32_t group_index = start / opt_bitmap.blocks_per_group;
    uint32_t block_in_group = start % opt_bitmap.blocks_per_group;
    struct block_group *group = &opt_bitmap.groups[group_index];
    
    for (uint32_t i = 0; i < blocks; i++) {
        clear_bit(block_in_group + i, group->bitmap);
    }
    
    group->needs_update = true;
    opt_bitmap.update_counter++;
}

// Optimized deallocation
static void opt_bitmap_free_range(int start, uint32_t blocks) {
    uint32_t group_index = start / opt_bitmap.blocks_per_group;
    uint32_t block_in_group = start % opt_bitmap.blocks_per_group;
    struct block_group *group = &opt_bitmap.groups[group_index];
    
    for (uint32_t i = 0; i < blocks; i++) {
        set_bit(block_in_group + i, group->bitmap);
    }
    
    group->needs_update = true;
    opt_bitmap.update_counter++;
}

static double calculate_opt_bitmap_fragmentation(void) {
    uint64_t total_free = 0;
    uint64_t fragmented = 0;
    uint64_t max_contiguous = 0;
    uint64_t num_free_runs = 0;
    uint32_t threshold_blocks = FRAGMENTATION_THRESHOLD / MIN_ALLOC;
    
    for (uint32_t g = 0; g < opt_bitmap.groups_count; g++) {
        struct block_group *group = &opt_bitmap.groups[g];
        uint32_t group_blocks = (g == opt_bitmap.groups_count - 1) ? 
                              opt_bitmap.last_group_blocks : opt_bitmap.blocks_per_group;
        
        uint32_t current_run = 0;
        for (uint32_t i = 0; i <= group_blocks; i++) {
            if (i < group_blocks && test_bit(i, group->bitmap)) {
                current_run++;
                total_free++;
            } else if (current_run > 0) {
                num_free_runs++;
                if (current_run > max_contiguous) {
                    max_contiguous = current_run;
                }
                if (current_run < threshold_blocks) {
                    fragmented += current_run;
                }
                current_run = 0;
            }
        }
    }
    
    printf("Largest contiguous free space: %" PRIu64 " blocks (%" PRIu64 " bytes)\n", 
           max_contiguous, max_contiguous * MIN_ALLOC);
    printf("Total free space: %" PRIu64 " blocks (%" PRIu64 " bytes)\n", 
           total_free, total_free * MIN_ALLOC);
    printf("Total fragmented space: %" PRIu64 " blocks (%" PRIu64 " bytes)\n", 
           fragmented, fragmented * MIN_ALLOC);
    printf("Number of free runs: %" PRIu64 "\n", num_free_runs);
    if (num_free_runs > 0) {
        printf("Average free run size: %" PRIu64 " blocks\n", total_free / num_free_runs);
    }
    
    return total_free ? ((double)fragmented * 100.0) / total_free : 0.0;
}

static void run_optimized_bitmap_benchmark(void) {
    printf("\nOptimized Bitmap Approach Results:\n");
    fflush(stdout);
    
    opt_bitmap_init();
    
    uint64_t start_time = get_time_us();
    uint64_t total_alloc_time = 0;
    uint64_t total_free_time = 0;
    int alloc_count = 0;
    int free_count = 0;
    
    // Array to track allocations for freeing
    struct {
        int start;
        uint32_t blocks;
    } *allocations = malloc(NUM_OPS * sizeof(*allocations));
    int alloc_index = 0;
    
    for (int i = 0; i < NUM_OPS; i++) {
        if ((double)rand() / RAND_MAX < ALLOC_PROB && alloc_index < NUM_OPS) {
            // Allocation
            uint32_t size = MIN_ALLOC + rand() % (MAX_ALLOC - MIN_ALLOC);
            uint32_t blocks_needed = (size + MIN_ALLOC - 1) / MIN_ALLOC;
            
            uint64_t before = get_time_us();
            int start = opt_bitmap_find_free_space(blocks_needed);
            if (start >= 0) {
                opt_bitmap_allocate_range(start, blocks_needed);
                allocations[alloc_index].start = start;
                allocations[alloc_index].blocks = blocks_needed;
                alloc_index++;
                total_alloc_time += get_time_us() - before;
                alloc_count++;
            }
        } else if (alloc_index > 0) {
            // Free
            int index = rand() % alloc_index;
            uint64_t before = get_time_us();
            opt_bitmap_free_range(allocations[index].start, allocations[index].blocks);
            total_free_time += get_time_us() - before;
            
            // Remove from tracking array
            allocations[index] = allocations[--alloc_index];
            free_count++;
        }
    }
    
    uint64_t total_time = get_time_us() - start_time;
    printf("Total time: %" PRIu64 " us\n", total_time);
    printf("Average allocation time: %" PRIu64 " us\n", 
           alloc_count ? total_alloc_time / alloc_count : 0);
    printf("Average free time: %" PRIu64 " us\n", 
           free_count ? total_free_time / free_count : 0);
    printf("Operations per second: %.2f\n", (NUM_OPS * 1000000.0) / total_time);
    
    double fragmentation = calculate_opt_bitmap_fragmentation();
    printf("Fragmentation ratio: %.2f%%\n", fragmentation);
    
    free(allocations);
    opt_bitmap_cleanup();
}

int main(void) {
    srand(time(NULL));
    
    printf("Running benchmarks with:\n");
    printf("Total space: %llu bytes\n", TOTAL_SPACE);
    printf("Min allocation: %d bytes\n", MIN_ALLOC);
    printf("Max allocation: %d bytes\n", MAX_ALLOC);
    printf("Number of operations: %d\n", NUM_OPS);
    printf("Allocation probability: %.2f\n\n", ALLOC_PROB);

    // Reset global variables
    rb_root = NULL;
    list_head = NULL;

    run_hybrid_benchmark();
    run_bitmap_benchmark();
    run_optimized_bitmap_benchmark();

    return 0;
} 

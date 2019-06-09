/* mem.c : memory manager
 */

#include <i386.h>
#include <xeroskernel.h>

/*-----------------------------------------------------------------------------------
 * This is the memory manager where we allocate memory from the free space and manage
 * the memory available to the kernel. It is used for dynamically allocating process
 * stacks.
 *
 * List of functions that are called from outside this file:
 * - kmeminit
 *   - Initializes the free list
 * - kmalloc
 *   - Returns a pointer to the start of the allocated block of memory if successful,
 *     0 if not enough memory is available
 * - kfree
 *   - Returns 1 on success, 0 on failure
 * - valid_ptr
 *   - Returns 1 if the given pointer is valid, 0 otherwise
 * - valid_buf
 *   - Returns 1 if the given buffer is valid, 0 otherwise
 * - get_free_list_length
 *   - Returns the length of the free list
 * - print_free_list
 *   - Prints the free list
 *-----------------------------------------------------------------------------------
 */

extern long freemem;    /* start of free memory (set in i386.c) */
extern char *maxaddr;   /* max memory address (set in i386.c)	*/

unsigned long freemem_aligned;
unsigned long hole_start_aligned;
unsigned long hole_end_aligned;
unsigned long max_addr_aligned;

typedef struct mem_header {
    // Size of block including header
    unsigned long size;
    struct mem_header *prev;
    struct mem_header *next;
    // NULL for free block, data_start for allocated block
    char *sanity_check;
    unsigned char data_start[0];
} mem_header_t;

static const unsigned long HEADER_SIZE = sizeof(mem_header_t);

static void split_off_free_block(size_t size, mem_header_t *block);
static int are_adjacent_blocks(mem_header_t *left_block, mem_header_t *right_block);
static void merge_blocks(mem_header_t *left_block, mem_header_t *right_block);
static int on_paragraph_boundary(unsigned long addr);
static unsigned long round_up_to_paragraph(unsigned long to_align);
static unsigned long round_down_to_paragraph(unsigned long to_align);
static int in_free_memory_range(unsigned long addr);
static int in_memory_range(unsigned long addr);
static int in_kernel_memory_range(unsigned long addr);

static mem_header_t *free_list;

/*-----------------------------------------------------------------------------------
 * To be only called by the kernel, before any memory allocations occur.
 * This function initializes the free list.
 *-----------------------------------------------------------------------------------
 */
void kmeminit(void) {
    kprintf("\nStarting kmeminit...\n");

    freemem_aligned = round_up_to_paragraph(freemem);
    hole_start_aligned = round_down_to_paragraph(HOLESTART);
    hole_end_aligned = round_up_to_paragraph(HOLEEND);
    max_addr_aligned = round_down_to_paragraph((unsigned long) maxaddr);

    // Initially, the free list should contain the block before the hole and the block after the hole
    // Initialize the block before the hole
    free_list = (mem_header_t *) freemem_aligned;
    free_list->size = hole_start_aligned - freemem_aligned;
    free_list->sanity_check = NULL;

    // Initialize the block after the hole
    mem_header_t *post_hole_block = (mem_header_t *) hole_end_aligned;
    post_hole_block->size = max_addr_aligned - hole_end_aligned;
    post_hole_block->sanity_check = NULL;

    // Link the blocks together
    free_list->prev = NULL;
    free_list->next = post_hole_block;
    post_hole_block->prev = free_list;
    post_hole_block->next = NULL;

    kprintf("Finished kmeminit\n");
}

/*-----------------------------------------------------------------------------------
 * To be only called by the kernel. Allocates a corresponding amount of memory
 * from the available memory and returns a pointer to the start of it.
 *
 * @param req_sz Number of bytes to allocate
 * @return       A pointer to the start of the allocated block of memory if successful,
 *               0 if not enough memory is available
 *-----------------------------------------------------------------------------------
 */
void *kmalloc(size_t req_sz) {
    size_t max_size = max_addr_aligned - freemem_aligned - HEADER_SIZE;
    if (req_sz <= 0 || req_sz > max_size) {
        return 0;
    }
    // Compute amount of memory to set aside for this request
    size_t size = round_up_to_paragraph(req_sz) + HEADER_SIZE;

    // Scan free list looking for block
    mem_header_t *mem_slot = free_list;
    while (mem_slot != NULL) {
        // If free block is large enough to allocate to, allocate to block
        if (size <= mem_slot->size) {
            if (size != mem_slot->size) {
                split_off_free_block(size, mem_slot);
            }
            // Fill in header fields
            mem_slot->size = size;
            mem_slot->sanity_check = (char *) mem_slot->data_start;

            // Remove block from free list
            if (mem_slot->prev) {
                mem_slot->prev->next = mem_slot->next;
            } else {
                // The first block was allocated, start free list at block after
                free_list = mem_slot->next;
            }
            if (mem_slot->next) {
                mem_slot->next->prev = mem_slot->prev;
            }

            unsigned long data_start = (unsigned long) mem_slot->data_start;
            assert(in_free_memory_range(data_start),
                   "Address returned by kmalloc is not in the range of allocatable memory");
            assert(on_paragraph_boundary(data_start), "Address returned by kmalloc is not on paragraph boundary");
            return mem_slot->data_start;
        }
        mem_slot = mem_slot->next;
    }
    // No suitable free blocks were found
    return 0;
}

/*-----------------------------------------------------------------------------------
 * Splits off a free block of the given size from the given block, creating another
 * free block with the bytes left over.
 *
 * @param block The free block to split from
 * @param size  The number of bytes to split off from the free block
 *-----------------------------------------------------------------------------------
 */
static void split_off_free_block(size_t size, mem_header_t *block) {
    mem_header_t *remaining_block = (mem_header_t *) ((size_t) block + size);
    remaining_block->size = block->size - size;
    remaining_block->sanity_check = NULL;

    remaining_block->prev = block;
    remaining_block->next = block->next;
    if (remaining_block->next != NULL) {
        remaining_block->next->prev = remaining_block;
    }

    block->size = size;
    block->next = remaining_block;
}

/*-----------------------------------------------------------------------------------
 * To be only called by the kernel. Takes a pointer to a previously allocated block
 * of memory and returns it to the free memory pool.
 *
 * @param ptr A pointer to a previously allocated block of memory
 * @return    1 on success, 0 on failure
 *-----------------------------------------------------------------------------------
 */
int kfree(void *ptr) {
    if (ptr == NULL) {
        return 0;
    }
    unsigned long addr = (unsigned long) ptr;
    if (!in_free_memory_range(addr) || !on_paragraph_boundary(addr)) {
        return 0;
    }
    // Determine start of allocated area
    mem_header_t *block_to_free = (mem_header_t *) (ptr - HEADER_SIZE);
    if (!in_free_memory_range((unsigned long) block_to_free)
        || !on_paragraph_boundary((unsigned long) block_to_free)
        || block_to_free->sanity_check != ptr) {
        return 0;
    }

    block_to_free->sanity_check = NULL;

    // Add memory back to free list
    mem_header_t *prev_mem_slot = NULL;
    mem_header_t *next_mem_slot = free_list;

    while (next_mem_slot != NULL && next_mem_slot < block_to_free) {
        prev_mem_slot = next_mem_slot;
        next_mem_slot = next_mem_slot->next;
    }
    // prev_mem_slot now points to the block that should be before the freed block
    // next_mem_slot now points to the block that should be after the freed block

    block_to_free->prev = prev_mem_slot;
    block_to_free->next = next_mem_slot;

    // Adjust free list pointers
    if (prev_mem_slot == NULL) {
        // Freed block is at the start
        free_list = block_to_free;
    } else {
        prev_mem_slot->next = block_to_free;
    }

    if (next_mem_slot != NULL) {
        next_mem_slot->prev = block_to_free;
    }

    // Merge freed block with any blocks in the free list that it is adjacent to
    // Merge if adjacent to next block
    if (are_adjacent_blocks(block_to_free, block_to_free->next)) {
        merge_blocks(block_to_free, block_to_free->next);
    }
    // Merge if adjacent to previous block
    if (are_adjacent_blocks(block_to_free->prev, block_to_free)) {
        merge_blocks(block_to_free->prev, block_to_free);
    }
    return 1;
}

/*-----------------------------------------------------------------------------------
 * Checks whether the given free blocks are adjacent.
 *
 * @param left_block  The left block to check
 * @param right_block The right block to check
 * @return            1 if blocks are adjacent, 0 otherwise
 *-----------------------------------------------------------------------------------
 */
static int are_adjacent_blocks(mem_header_t *left_block, mem_header_t *right_block) {
    if (left_block == NULL || right_block == NULL) {
        return 0;
    }
    unsigned long left_block_addr = (unsigned long) left_block;
    unsigned long right_block_addr = (unsigned long) right_block;
    if (left_block_addr + left_block->size == right_block_addr) {
        return 1;
    } else {
        return 0;
    }
}

/*-----------------------------------------------------------------------------------
 * Merges the given free blocks into one. Will merge the right block into the left.
 *
 * @param left_block  The left block to merge
 * @param right_block The right block to merge, will become part of left block
 *-----------------------------------------------------------------------------------
 */
static void merge_blocks(mem_header_t *left_block, mem_header_t *right_block) {
    left_block->size = left_block->size + right_block->size;
    left_block->next = right_block->next;
    if (left_block->next != NULL) {
        left_block->next->prev = left_block;
    }
}

/*-----------------------------------------------------------------------------------
 * Checks whether given address is on a paragraph boundary.
 *
 * @param ptr The address to check
 * @return    1 if address is on paragraph boundary, 0 otherwise
 *-----------------------------------------------------------------------------------
 */
static int on_paragraph_boundary(unsigned long addr) {
    return addr % PARAGRAPH_SIZE == 0;
}

/*-----------------------------------------------------------------------------------
 * Checks whether given address is in the range of possible allocatable memory.
 *
 * @param ptr The address to check
 * @return    1 if address is in the range of possible allocatable memory, 0 otherwise
 *-----------------------------------------------------------------------------------
 */
static int in_free_memory_range(unsigned long addr) {
    int in_pre_hole = addr >= freemem_aligned && addr <= hole_start_aligned;
    int in_post_hole = addr >= hole_end_aligned && addr <= max_addr_aligned;
    return in_pre_hole || in_post_hole;
}

/*-----------------------------------------------------------------------------------
 * Rounds an address or size up to the nearest paragraph boundary.
 *
 * @param  to_align The address or size to round up
 * @return The address or size rounded up to the nearest paragraph boundary
 *-----------------------------------------------------------------------------------
 */
static unsigned long round_up_to_paragraph(unsigned long to_align) {
    unsigned long num_blocks = (to_align) / PARAGRAPH_SIZE + ((to_align % PARAGRAPH_SIZE) ? 1 : 0);
    return num_blocks * PARAGRAPH_SIZE;
}

/*-----------------------------------------------------------------------------------
 * Rounds an address or size down to the nearest paragraph boundary.
 *
 * @param  to_align The address or size to round down
 * @return The address or size rounded down to the nearest paragraph boundary
 *-----------------------------------------------------------------------------------
 */
static unsigned long round_down_to_paragraph(unsigned long to_align) {
    unsigned long num_blocks = (unsigned long) to_align / PARAGRAPH_SIZE;
    return num_blocks * PARAGRAPH_SIZE;
}

/*-----------------------------------------------------------------------------------
 * Checks if a given pointer is valid.
 *
 * @param ptr The pointer to check
 * @return    1 if the pointer is valid, 0 otherwise
 *-----------------------------------------------------------------------------------
 */
int valid_ptr(void *ptr) {
    return ptr != NULL && in_memory_range((unsigned long) ptr);
}

/*-----------------------------------------------------------------------------------
 * Checks if a given buffer is valid.
 *
 * @param ptr    The pointer to check
 * @param length The length of the buffer
 * @return       1 if the buffer is valid, 0 otherwise
 *-----------------------------------------------------------------------------------
 */
int valid_buf(void *ptr, unsigned long length) {
    if (valid_ptr(ptr) && !in_kernel_memory_range((unsigned long) ptr) && length > 0) {
        unsigned long buf_start = (unsigned long) ptr;
        unsigned long buf_end = buf_start + length;
        return in_memory_range(buf_end) && !in_kernel_memory_range(buf_end);
    }
    return 0;
}

/*-----------------------------------------------------------------------------------
 * Checks if the given address is in the valid range for a process.
 *
 * @param addr The address to check
 * @return     1 if the address is in the valid range, 0 otherwise
 *-----------------------------------------------------------------------------------
 */
static int in_memory_range(unsigned long addr) {
    int in_pre_hole = addr > 0 && addr < HOLESTART;
    int in_post_hole = addr >= HOLEEND && addr < (unsigned long) maxaddr;
    return in_pre_hole || in_post_hole;
}

/*-----------------------------------------------------------------------------------
 * Checks if the given address is in the range of kernel memory.
 *
 * @param addr The address to check
 * @return     1 if the address is in the range of kernel memory, 0 otherwise
 *-----------------------------------------------------------------------------------
 */
static int in_kernel_memory_range(unsigned long addr) {
    return addr > 0 && addr < freemem_aligned;
}

/* Functions for testing */

/*-----------------------------------------------------------------------------------
 * Returns the length of the free list.
 *
 * @return The length of the free list
 *-----------------------------------------------------------------------------------
 */
int get_free_list_length(void) {
    if (free_list == NULL) {
        return 0;
    }
    int length = 0;
    mem_header_t *curr = free_list;
    while (curr != NULL) {
        length++;
        curr = curr->next;
    }
    return length;
}

/*-----------------------------------------------------------------------------------
 * Prints the free list.
 *-----------------------------------------------------------------------------------
 */
void print_free_list(void) {
    kprintf("\nFree list:\n");
    if (free_list == NULL) {
        kprintf("Empty\n");
    }
    int count = 0;
    mem_header_t *curr = free_list;
    while (curr != NULL) {
        count++;
        kprintf("Block %d: 0x%x\n", count, curr);
        curr = curr->next;
    }
}

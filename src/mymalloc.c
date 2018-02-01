/*
Author: Benjamin Runco
An implementation of malloc & free.
*/

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "mymalloc.h"

// The smallest allocation possible is this many bytes.
// Any allocations <= this size will b put in bin 0.
#define MINIMUM_ALLOCATION  16

// Every bin holds blocks whose sizes are a multiple of this number.
#define SIZE_MULTIPLE       8

// The biggest bin holds blocks of this size. Anything bigger will go in the overflow bin.
#define BIGGEST_BINNED_SIZE 512

// How many bins there are. There's an "underflow" bin (bin 0) and an overflow bin (the last bin).
// That's where the '2' comes from in this formula.
#define NUM_BINS            (2 + ((BIGGEST_BINNED_SIZE - MINIMUM_ALLOCATION) / SIZE_MULTIPLE))

// The index of the overflow bin.
#define OVERFLOW_BIN        (NUM_BINS - 1)

// How many bytes the block header is, in a USED block.
// NEVER USE sizeof(BlockHeader) in your calculations! Use this instead.
#define BLOCK_HEADER_SIZE   offsetof(BlockHeader, prev_free)

// The smallest number of bytes a block (including header and data) can be.
#define MINIMUM_BLOCK_SIZE  (MINIMUM_ALLOCATION + BLOCK_HEADER_SIZE)

typedef struct BlockHeader
{
	unsigned int size; // The byte size of the data area of this block.
	int in_use;        // 1 if allocated, 0 if free.

	// Doubly-linked list pointers for the previous and next *physical* blocks of memory.
	// All blocks, allocated or free, must keep track of this for coalescing purposes.
	struct BlockHeader* prev_phys;
	struct BlockHeader* next_phys;

	// These next two members are only valid if the block is not in use (on a free list).
	// If the block is in use, the user-allocated data starts here instead!
	struct BlockHeader* prev_free;
	struct BlockHeader* next_free;
} BlockHeader;

// Your array of bins.
BlockHeader* bins[NUM_BINS] = {};

// The LAST allocated block on the heap.
// This is used to keep track of when you should contract the heap.
BlockHeader* heap_tail = NULL;

// =================================================================================================
// Math helpers
// =================================================================================================

// Given a pointer and a number of bytes, gives a new pointer that points to the original address
// plus or minus the offset. The offset can be negative.
// Since this returns a void*, you have to cast the result to another pointer type to use it.
void* ptr_add_bytes(void* ptr, int byte_offs)
{
	return (void*)(((char*)ptr) + byte_offs);
}

// Gives the number of bytes between the two pointers. first must be <= second.
unsigned int bytes_between_ptrs(void* first, void* second)
{
	return (unsigned int)(((char*)second) - ((char*)first));
}

// Given a pointer to a block header, gives the pointer to its data (such as what you'd return
// from my_malloc).
void* block_to_data(BlockHeader* block)
{
	return (void*)ptr_add_bytes(block, BLOCK_HEADER_SIZE);
}

// Given a data pointer (such as passed to my_free()), gives the pointer to the block that
// contains it.
BlockHeader* data_to_block(void* data)
{
	return (BlockHeader*)ptr_add_bytes(data, -BLOCK_HEADER_SIZE);
}

// Given a data size, gives how many bytes you'd need to allocate for a block to hold it.
unsigned int data_size_to_block_size(unsigned int data_size)
{
	return data_size + BLOCK_HEADER_SIZE;
}

// Rounds up a data size to an appropriate size for putting into a bin.
unsigned int round_up_size(unsigned int data_size)
{
	if(data_size == 0)
		return 0;
	else if(data_size < MINIMUM_ALLOCATION)
		return MINIMUM_ALLOCATION;
	else
		return (data_size + (SIZE_MULTIPLE - 1)) & ~(SIZE_MULTIPLE - 1);
}

// Given a data size in bytes, gives the correct bin index to put it in.
unsigned int size_to_bin(unsigned int data_size)
{
	unsigned int bin = (round_up_size(data_size) - MINIMUM_ALLOCATION) / SIZE_MULTIPLE;

	if(bin > OVERFLOW_BIN)
		return OVERFLOW_BIN;
	else
		return bin;
}

// links block onto end of physical list
void link_onto_end(BlockHeader* new_block)
{
	if(heap_tail == NULL) {
		new_block->prev_phys = NULL;
		new_block->next_phys = NULL;
		heap_tail = new_block;
		return;
	}

	BlockHeader* old_tail = heap_tail;
	old_tail->next_phys = new_block;
	new_block->next_phys = NULL;
	new_block->prev_phys = old_tail;
	heap_tail = new_block;
}

// unlinks block from end of physical list
void unlink_block() {

	if(heap_tail == NULL) {
		return;
	}

	if(heap_tail->prev_phys == NULL) {
		heap_tail = NULL;
		return;
	}

	BlockHeader* new_tail = heap_tail->prev_phys;
	new_tail->next_phys = NULL;
	heap_tail = new_tail;
}

// inserts block at the head of appropriate sized bin
void insert_into_bin(BlockHeader* block) {

	unsigned int bin_index = size_to_bin(block->size);

	if(bins[bin_index] == NULL) {
		block->prev_free = NULL;
		block->next_free = NULL;
		bins[bin_index] = block;
		return;
	}

	BlockHeader* old_head = bins[bin_index];
	old_head->prev_free = block;
	block->next_free = old_head;
	block->prev_free = NULL;
	bins[bin_index] = block;

}

// removes block from appropriate sized free list
void remove_block(BlockHeader* block) {

	unsigned int bin_index = size_to_bin(block->size);

	// block is only one in the free list
	if(block->prev_free == NULL && block->next_free == NULL) {
		bins[bin_index] = NULL;
		return;
	}

	// block is at the head of the free list, but not the only block
	if(block->prev_free == NULL && block->next_free != NULL) {
		bins[bin_index] = block->next_free;
		block->next_free->prev_free = NULL; // change here
		return;
	}

	// block is at the end of the free list
	if(block->prev_free != NULL && block->next_free == NULL) {
		BlockHeader* prev_block = block->prev_free;
		prev_block->next_free = NULL;
		return;
	}

	// block is a "middle" block
	if(block->prev_free != NULL && block->next_free != NULL) {
		BlockHeader* prev_block = block->prev_free;
		BlockHeader* next_block = block->next_free;
		prev_block->next_free = next_block;
		next_block->prev_free = prev_block;
		return;
	}
}

BlockHeader* coalesce(BlockHeader* block) {

	if(block->prev_phys != NULL && block->next_phys != NULL) {
		// has both a next and previous neighbor

		if(block->prev_phys->in_use == 0 && block->next_phys->in_use == 0) {
			// coalesce both neighbors

			remove_block(block->next_phys);
			remove_block(block->prev_phys);

			int new_size = block->prev_phys->size + (block->size + BLOCK_HEADER_SIZE) + (block->next_phys->size + BLOCK_HEADER_SIZE);
			BlockHeader* coalesced_block = block->prev_phys;
			coalesced_block->size = new_size;
			coalesced_block->in_use = 0;

			coalesced_block->next_phys = block->next_phys->next_phys;

			if(coalesced_block->next_phys == NULL) {
				heap_tail = coalesced_block;
			} else {
				coalesced_block->next_phys->prev_phys = coalesced_block;
			}
			return coalesced_block;

		} else if(block->prev_phys->in_use == 0 && block->next_phys->in_use == 1) {
			// coalesce previous neighbor

			remove_block(block->prev_phys);

			int new_size = block->prev_phys->size + (block->size + BLOCK_HEADER_SIZE);
			BlockHeader* coalesced_block = block->prev_phys;
			coalesced_block->size = new_size;
			coalesced_block->in_use = 0;

			coalesced_block->next_phys = block->next_phys;
			coalesced_block->next_phys->prev_phys = coalesced_block;

			return coalesced_block;

		} else if(block->prev_phys->in_use == 1 && block->next_phys->in_use == 0) {
			// coalesce next neighbor

			remove_block(block->next_phys);

			int new_size = block->next_phys->size + (block->size + BLOCK_HEADER_SIZE);
			BlockHeader* coalesced_block = block;
			coalesced_block->size = new_size;
			coalesced_block->in_use = 0;

			coalesced_block->next_phys = block->next_phys->next_phys;

			if(coalesced_block->next_phys == NULL) {
				heap_tail = coalesced_block;
			} else {
				coalesced_block->next_phys->prev_phys = coalesced_block;
			}
			return coalesced_block;
		}

	} else if(block->prev_phys != NULL && block->next_phys == NULL) {
		// only has a previous neighbor
		if(block->prev_phys->in_use == 0) {
			// coalesce previous neighbor
			remove_block(block->prev_phys);

			int new_size = block->prev_phys->size + (block->size + BLOCK_HEADER_SIZE);
			BlockHeader* coalesced_block = block->prev_phys;
			coalesced_block->size = new_size;
			coalesced_block->in_use = 0;

			coalesced_block->next_phys = NULL;
			heap_tail = coalesced_block;
			return coalesced_block;
		}

	} else if(block->prev_phys == NULL && block->next_phys != NULL) {
	 	// only has a next neighbor
		if(block->next_phys->in_use == 0) {
			// coalesce next neighbor
			remove_block(block->next_phys);

			int new_size = block->next_phys->size + (block->size + BLOCK_HEADER_SIZE);
			BlockHeader* coalesced_block = block;
			coalesced_block->size = new_size;
			coalesced_block->in_use = 0;

			coalesced_block->next_phys = block->next_phys->next_phys;

			if(coalesced_block->next_phys == NULL) {
				heap_tail = coalesced_block;
			} else {
				coalesced_block->next_phys->prev_phys = coalesced_block;
			}
			return coalesced_block;
		}
	}
	return block;
}

BlockHeader* split_block(BlockHeader* block, int allocation_size) {

	int old_block_size = block->size;

	// remove unsplit block from appropriate free list
	remove_block(block);

	BlockHeader* allocated_portion = block;
	BlockHeader* empty_portion = ptr_add_bytes(block, allocation_size+BLOCK_HEADER_SIZE);
	BlockHeader* next_block = block->next_phys;

	allocated_portion->in_use = 1;
	allocated_portion->size = allocation_size;
	empty_portion->in_use = 0;
	empty_portion->size = (old_block_size - allocation_size) - BLOCK_HEADER_SIZE;

	// link allocated portion and empty portion
	allocated_portion->next_phys = empty_portion;
	empty_portion->prev_phys = allocated_portion;

	// link empty portion with next block
	empty_portion->next_phys = next_block;

	if(next_block != NULL) {
		next_block->prev_phys = empty_portion;
	} else {
		heap_tail = empty_portion;
	}

	// insert empty portion of split block to appropriate bin
	insert_into_bin(empty_portion);

	return allocated_portion;
}

// =================================================================================================
// Public functions
// =================================================================================================

void* my_malloc(unsigned int size)
{
	if(size == 0)
		return NULL;

	size = round_up_size(size);

	unsigned int bin_index = size_to_bin(size);

	BlockHeader* current = bins[bin_index];
	BlockHeader* new_allocation = NULL;

	if(bin_index < OVERFLOW_BIN && current != NULL) {
		// remove block from a small bin
		new_allocation = current;
		new_allocation->in_use = 1;
		remove_block(current);
	} else if(bin_index < OVERFLOW_BIN && current == NULL) {
		// try to find block big enough to split in the small bins
		BlockHeader* block_to_split = NULL;
		int index;
		for(index = size_to_bin(size + MINIMUM_BLOCK_SIZE); index < OVERFLOW_BIN; index++) {

			if(bins[index] != NULL) {

				block_to_split = bins[index];

				if((block_to_split->size - size) < MINIMUM_BLOCK_SIZE) {
					block_to_split = NULL;
					continue;
				}
				remove_block(block_to_split);
				break;
			}
		}
		// if one found, split it
		if(block_to_split != NULL) {
			new_allocation = split_block(block_to_split, size);
		}
	}

	if(new_allocation == NULL) {
		current = bins[OVERFLOW_BIN];
		BlockHeader* block_to_split = NULL;

		// first fit
		while(current != NULL) {
			if(current->size >= size) {

				if(((current->size - size) >= MINIMUM_BLOCK_SIZE)) {
					// split block
					block_to_split = current;
					new_allocation = split_block(block_to_split, size);
				} else {
					// use full block
					new_allocation = current;
					new_allocation->in_use = 1;
					remove_block(new_allocation);
				}
				break;
			} else {
				current = current->next_free;
			}
		}
	}

	// free block of correct size not found, allocate new block
	if(new_allocation == NULL) {
		new_allocation = sbrk(size+BLOCK_HEADER_SIZE);
		new_allocation->size = size;
		new_allocation->in_use = 1;
		link_onto_end(new_allocation);
	}

	return block_to_data(new_allocation);

	// ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
}

void my_free(void* ptr)
{
	if(ptr == NULL)
		return;

	BlockHeader* block_to_free = data_to_block(ptr);
	block_to_free = coalesce(block_to_free);

	if(block_to_free->next_phys == NULL) {
		unlink_block(); //unlink end block
		brk(block_to_free);
	}else{
		block_to_free->in_use = 0;
		insert_into_bin(block_to_free);
	}

}

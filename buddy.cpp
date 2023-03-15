/*
 * The Buddy Page Allocator
 * SKELETON IMPLEMENTATION TO BE FILLED IN FOR TASK 2
 */

#include <infos/mm/page-allocator.h>
#include <infos/mm/mm.h>
#include <infos/kernel/kernel.h>
#include <infos/kernel/log.h>
#include <infos/util/math.h>
#include <infos/util/printf.h>

using namespace infos::kernel;
using namespace infos::mm;
using namespace infos::util;

#define MAX_ORDER	18

/**
 * A buddy page allocation algorithm.
 */
class BuddyPageAllocator : public PageAllocatorAlgorithm
{
private:

	/**
	 * Returns the number of pages that comprise a 'block', in a given order.
	 * @param order The order to base the calculation off of.
	 * @return Returns the number of pages in a block, in the order.
	 */
	static inline constexpr uint64_t pages_per_block(int order)
	{
		/* The number of pages per block in a given order is simply 1, shifted left by the order number.
		 * For example, in order-2, there are (1 << 2) == 4 pages in each block.
		 */
		return (1 << order);
	}

	/**
	 * Returns TRUE if the supplied page descriptor is correctly aligned for the 
	 * given order.  Returns FALSE otherwise.
	 * @param pgd The page descriptor to test alignment for.
	 * @param order The order to use for calculations.
	 */
	static inline bool is_correct_alignment_for_order(const PageDescriptor *pgd, int order)
	{
		// Calculate the page-frame-number for the page descriptor, and return TRUE if
		// it divides evenly into the number pages in a block of the given order.
		return (sys.mm().pgalloc().pgd_to_pfn(pgd) % pages_per_block(order)) == 0;
	}

	/** Given a page descriptor, and an order, returns the buddy PGD.  The buddy could either be
	 * to the left or the right of PGD, in the given order.
	 * @param pgd The page descriptor to find the buddy for.
	 * @param order The order in which the page descriptor lives.
	 * @return Returns the buddy of the given page descriptor, in the given order.
	 */
	PageDescriptor *buddy_of(PageDescriptor *pgd, int order)
	{
        // (1) Make sure 'order' is within range
		if (order >= MAX_ORDER) 
		{
			return NULL;
		}

		// (2) Check to make sure that PGD is correctly aligned in the order
		if (!is_correct_alignment_for_order(pgd, order)) 
		{
			return NULL;
		}

		// (3) Calculate the page-frame-number of the buddy of this page.
		// * If the PFN is aligned to the next order, then the buddy is the next block in THIS order.
		// * If it's not aligned, then the buddy must be the previous block in THIS order.
		uint64_t buddy_pfn = is_correct_alignment_for_order(pgd, order + 1) ?
			sys.mm().pgalloc().pgd_to_pfn(pgd) + pages_per_block(order) : 
			sys.mm().pgalloc().pgd_to_pfn(pgd) - pages_per_block(order);

		// (4) Return the page descriptor associated with the buddy page-frame-number.
		return sys.mm().pgalloc().pfn_to_pgd(buddy_pfn);
	}

	/**
	 * Inserts a block into the free list of the given order.  The block is inserted in ascending order.
	 * @param pgd The page descriptor of the block to insert.
	 * @param order The order in which to insert the block.
	 * @return Returns the slot (i.e. a pointer to the pointer that points to the block) that the block
	 * was inserted into.
	 */
	PageDescriptor **insert_block(PageDescriptor *pgd, int order)
	{
		// Starting from the _free_area array, find the slot in which the page descriptor
		// should be inserted.
		PageDescriptor **slot = &_free_areas[order];

		// Iterate whilst there is a slot, and whilst the page descriptor pointer is numerically
		// greater than what the slot is pointing to.
		while (*slot && pgd > *slot) 
		{
			slot = &(*slot)->next_free;
		}

		// Insert the page descriptor into the linked list.
		pgd->next_free = *slot;
		*slot = pgd;

		// Return the insert point (i.e. slot)
		return slot;
	}

	/**
	 * Removes a block from the free list of the given order.  The block MUST be present in the free-list, otherwise
	 * the system will panic.
	 * @param pgd The page descriptor of the block to remove.
	 * @param order The order in which to remove the block from.
	 */
	void remove_block(PageDescriptor *pgd, int order)
	{
		// Starting from the _free_area array, iterate until the block has been located in the linked-list.
		PageDescriptor **slot = &_free_areas[order];
		while (*slot && pgd != *slot) 
		{
			slot = &(*slot)->next_free;
		}

		// Make sure the block actually exists.  Panic the system if it does not.
		assert(*slot == pgd);

		// Remove the block from the free list.
		*slot = pgd->next_free;
		pgd->next_free = NULL;
	}

	/**
	 * Given a pointer to a block of free memory in the order "source_order", this function will
	 * split the block in half, and insert it into the order below.
	 * @param block_pointer A pointer to a pointer containing the beginning of a block of free memory.
	 * @param source_order The order in which the block of free memory exists.  Naturally,
	 * the split will insert the two new blocks into the order below.
	 * @return Returns the left-hand-side of the new block.
	 */
	PageDescriptor *split_block(PageDescriptor **block_pointer, int source_order)
	{
		// Make sure there is an incoming pointer.
		assert(*block_pointer);

		// Make sure the area_pointer is correctly aligned.
		assert(is_correct_alignment_for_order(*block_pointer, source_order));

		// if the order is 0, then we cannot split the block
		if (source_order == 0) 
		{
			return *block_pointer;
		}

        // get the left and right side of the block
		PageDescriptor *left = *block_pointer;
		PageDescriptor *right = left + pages_per_block(source_order - 1);

		// remove the original block, and replace it with the split block at the lower order
		remove_block(left, source_order);
		insert_block(left, source_order - 1);
		insert_block(right, source_order - 1);

		return *block_pointer;
	}

	/**
	 * Takes a block in the given source order, and merges it (and its buddy) into the next order.
	 * @param block_pointer A pointer to a pointer containing a block in the pair to merge.
	 * @param source_order The order in which the pair of blocks live.
	 * @return Returns the new slot that points to the merged block.
	 */
	PageDescriptor **merge_block(PageDescriptor **block_pointer, int source_order)
	{
		// Make sure there is an incoming pointer.
		assert(*block_pointer);

		// Make sure the area_pointer is correctly aligned.
		assert(is_correct_alignment_for_order(*block_pointer, source_order));

        // get the block and its buddy, and remove them
		PageDescriptor *left = *block_pointer;
		PageDescriptor *right = buddy_of(left, source_order);
		remove_block(left, source_order);
		remove_block(right, source_order);

		// check for correct alignment, and insert the appropriately merged block at the higher order
		if (is_correct_alignment_for_order(left, source_order + 1)) 
		{
			return insert_block(left, source_order + 1);
		}
		return insert_block(right, source_order + 1);
		
	}

public:
	/**
	 * Allocates 2^order number of contiguous pages
	 * @param order The power of two, of the number of contiguous pages to allocate.
	 * @return Returns a pointer to the first page descriptor for the newly allocated page range, or NULL if
	 * allocation failed.
	 */
	PageDescriptor *allocate_pages(int order) override
	{
		int free;
		for (free = order; free <= MAX_ORDER; free++) 
		{
			// if we have reached the max order, return null since allocation failed
			if (free == MAX_ORDER && _free_areas[free] == NULL) return NULL;
			// otherwise continue until the right order is found
			else if (_free_areas[free] == NULL) continue;
			// break when correct order is found
			else break;
		}

		// go backwards and keep splitting the block
		PageDescriptor* block = _free_areas[free];
		for (int i = free; i > order; i--) 
		{
			block = split_block(&block, i);
		}

		// lastly, remove the block
		remove_block(block, order);
		return block;
	}

	/**
	* Helper function, repeatedly merges blocks until they can no longer be merged
	* @param block_pointer A pointer to a pointer containing a block
	* @param order The power of two, of the number of contiguous pages
	*/
	void repeated_merge(PageDescriptor **block_pointer, int order) 
	{
		// get the current slot (from parameter), current block for given order (from free areas) and the buddy of the current slot
		int curr_order = order;
		PageDescriptor **curr_block = block_pointer;
		PageDescriptor *block = _free_areas[curr_order];
		PageDescriptor *buddy = buddy_of(*curr_block, curr_order);

		// as long as we haven't reached max order and there are free areas in the current order
		while (curr_order < MAX_ORDER && block) 
		{
			// keep searching for a buddy in the free slots
			if (block != buddy) 
			{
				block = block->next_free;
			}
			// if buddy is free, merge and move to a higher order
			else 
			{
				curr_block = merge_block(curr_block, curr_order++);
				block = _free_areas[curr_order];
				buddy = buddy_of(*curr_block, curr_order);
			}
		}

	}

    /**
	 * Frees 2^order contiguous pages.
	 * @param pgd A pointer to an array of page descriptors to be freed.
	 * @param order The power of two number of contiguous pages to free.
	 */
    void free_pages(PageDescriptor *pgd, int order) override
    {
		// Make sure that the incoming page descriptor is correctly aligned
		// for the order on which it is being freed, for example, it is
		// illegal to free page 1 in order-1.
		assert(is_correct_alignment_for_order(pgd, order));
		
		// free requested block, and then continuously merge blocks until it is no longer possible
		PageDescriptor **block = insert_block(pgd, order);
		repeated_merge(block, order);
    }

    /**
     * Marks a range of pages as available for allocation.
     * @param start A pointer to the first page descriptors to be made available.
     * @param count The number of page descriptors to make available.
     */
    virtual void insert_page_range(PageDescriptor *start, uint64_t count) override
    {
		while (count > 0) 
		{
			int size;
			for (size = MAX_ORDER; size >= 0; size--) 
			{
				// loop through all blocks until the correct range of pages is found
				if (pages_per_block(size) > count || !is_correct_alignment_for_order(start, size)) continue;
				else break;
			}

			// free all pages for that order
			free_pages(start, size);
			// and update the start and count
			start += pages_per_block(size);
			count -= pages_per_block(size);
		}
    }

    /**
     * Marks a range of pages as unavailable for allocation.
     * @param start A pointer to the first page descriptors to be made unavailable.
     * @param count The number of page descriptors to make unavailable.
     */
    virtual void remove_page_range(PageDescriptor *start, uint64_t count) override
    {
		// base case
		if (count == 0) return;

		// convert page descriptor for start of remove range to a numeric format
		pfn_t start_as_pfn = sys.mm().pgalloc().pgd_to_pfn(start);
		pfn_t end_as_pfn = start_as_pfn + count - 1;

		// loop through all possible orders top-down
		for (uint32_t order = MAX_ORDER; order >= 0; order--) 
		{
			int curr_block_size = pages_per_block(order);
			PageDescriptor *curr_block = _free_areas[order];

			// as long as the block is not NULL
			while (curr_block) 
			{
				// convert page descriptor for the current block to a numeric format
				pfn_t block_start = sys.mm().pgalloc().pgd_to_pfn(curr_block);
				pfn_t block_end = block_start + curr_block_size - 1;

				// if this is the case, then we break, since the block does not contain the range at all
				if (block_start > start_as_pfn) break;

				// if the block contains at least part of the range
				if (block_start <= start_as_pfn && start_as_pfn <= block_end) 
				{
					// remove the whole block
					remove_block(curr_block, order);

					PageDescriptor *left = sys.mm().pgalloc().pfn_to_pgd(block_start);

					// if the range is fully contained in the block
					if (end_as_pfn <= block_end) 
					{
						PageDescriptor *right = sys.mm().pgalloc().pfn_to_pgd(end_as_pfn + 1);

						// we re-add the parts of the block outside the remove range
						insert_page_range(left, start_as_pfn - block_start);
						insert_page_range(right, block_start + curr_block_size - end_as_pfn - 1);
					}

					// if not fully contained, then the right side must be in a different block
					else 
					{
						PageDescriptor *right = sys.mm().pgalloc().pfn_to_pgd(block_end + 1);

						// so we insert the part on the left side that is outside the remove range
						insert_page_range(left, start_as_pfn - block_start);

						// and recurse to find the remaining part of the remove range
						remove_page_range(right, count - (block_end - start_as_pfn + 1));
					}

					// if reached, it means block was found, so terminate the function
					return;
				}

				// otherwise we move on to the next block
				curr_block = curr_block->next_free;
			}

		}

    }

	/**
	 * Initialises the allocation algorithm.
	 * @return Returns TRUE if the algorithm was successfully initialised, FALSE otherwise.
	 */
	bool init(PageDescriptor *page_descriptors, uint64_t nr_page_descriptors) override
	{
		// when initialising, mark all blocks as free
        for (unsigned int i = 0; i <= MAX_ORDER; i++) 
		{
			_free_areas[i] = NULL;
		}

		// base condition to ensure the parameters are valid
		return (page_descriptors && nr_page_descriptors > 0);
	}

	/**
	 * Returns the friendly name of the allocation algorithm, for debugging and selection purposes.
	 */
	const char* name() const override { return "buddy"; }

	/**
	 * Dumps out the current state of the buddy system
	 */
	void dump_state() const override
	{
		// Print out a header, so we can find the output in the logs.
		mm_log.messagef(LogLevel::DEBUG, "BUDDY STATE:");

		// Iterate over each free area.
		for (unsigned int i = 0; i < ARRAY_SIZE(_free_areas); i++) {
			char buffer[256];
			snprintf(buffer, sizeof(buffer), "[%d] ", i);

			// Iterate over each block in the free area.
			PageDescriptor *pg = _free_areas[i];
			while (pg) {
				// Append the PFN of the free block to the output buffer.
				snprintf(buffer, sizeof(buffer), "%s%lx ", buffer, sys.mm().pgalloc().pgd_to_pfn(pg));
				pg = pg->next_free;
			}

			mm_log.messagef(LogLevel::DEBUG, "%s", buffer);
		}
	}


private:
	PageDescriptor *_free_areas[MAX_ORDER+1];
};

/* --- DO NOT CHANGE ANYTHING BELOW THIS LINE --- */

/*
 * Allocation algorithm registration framework
 */
RegisterPageAllocator(BuddyPageAllocator);
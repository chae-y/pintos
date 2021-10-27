/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/vaddr.h"
// Project 3.5_Swap in/out
#include <bitmap.h>
#include "threads/mmu.h"
#include "threads/malloc.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

// Project 3.2_anonymous page
const size_t SECTORS_PER_PAGE = PGSIZE / DISK_SECTOR_SIZE;
// Project 3.2_end

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

static const struct page_operations anon_stack_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON | VM_MARKER_0,
};
static struct bitmap *swap_table;

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	// Project 3.2_anonymous page
	// swap_disk = NULL;
	swap_disk = disk_get(1,1);
	size_t swap_size = disk_size(swap_disk) / SECTORS_PER_PAGE;
	swap_table = bitmap_create(swap_size);
	// Project 3.2_end
}

/* Initialize the file mapping */
// Project 3.5_memory mapped file
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;
	if(type & VM_MARKER_0) page->operations = &anon_stack_ops; // VM_MARKER_0 : STACK
	struct anon_page *anon_page = &page->anon;
	anon_page->owner = thread_current();
	anon_page->swap_slot_idx = INVALID_SLOT_IDX;
	return true;
}

/* Swap in the page by read contents from the swap disk. */
// Project 3.5_memory mapped file
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
	if(anon_page->swap_slot_idx == INVALID_SLOT_IDX) return false;
	disk_sector_t sec_no;
	// Read page from disk with sector size chunk
	for (int i = 0; i < SECTORS_PER_PAGE; i++){
		// convert swap slot index to reading sector number
		sec_no = (disk_sector_t)(anon_page->swap_slot_idx * SECTORS_PER_PAGE) + i;
		off_t ofs = i * DISK_SECTOR_SIZE;
		disk_read (swap_disk, sec_no, kva+ofs);
	}
	// Clear swap table
	bitmap_set (swap_table, anon_page->swap_slot_idx, false);
	anon_page->swap_slot_idx = INVALID_SLOT_IDX;

	return true;
}

/* Swap out the page by writing contents to the swap disk. */
// Project 3.5_memory mapped file
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;

	// Get swap slot index from swap table
	size_t swap_slot_idx = bitmap_scan_and_flip (swap_table,0,1,false);
	if (swap_slot_idx == BITMAP_ERROR)
		PANIC("There is no free swap slot!");

	// Copy page frame content to swap_slot
	if (page == NULL || page->frame == NULL || page->frame->kva == NULL) // kva는 page fault 시 page align
		return false;

	disk_sector_t sec_no;
	// Write page to disk with sector size chunk
	for (int i = 0; i < SECTORS_PER_PAGE; i++){
		// Convert swap slot index to writing sector number
		// printf("swap_slot_idx : %d", swap_slot_idx);
		sec_no = (disk_sector_t)(swap_slot_idx * SECTORS_PER_PAGE) + i; // swap_slot_idx는 몇 번째 page 인지 !
		off_t ofs = i * DISK_SECTOR_SIZE;
		disk_write (swap_disk, sec_no, page->frame->kva + ofs);
	}
	anon_page->swap_slot_idx = swap_slot_idx;

	// Set "not present" to page, and clear.
	pml4_clear_page (anon_page->owner->pml4, page->va);
	pml4_set_dirty (anon_page->owner->pml4, page->va, false);
	page->frame = NULL;

	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	if (page -> frame!= NULL){
		list_remove (&page->frame->frame_elem);
		free(page->frame);
	}
	else {
		// Swapped anon page case
		struct anon_page *anon_page = &page->anon;
		ASSERT (anon_page->swap_slot_idx != INVALID_SLOT_IDX);

		// Clear swap table
		bitmap_set (swap_table, anon_page->swap_slot_idx, false);
	}
}
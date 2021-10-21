/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
// Porject 3.1_memory management
#include "lib/kernel/hash.h"
#include "threads/vaddr.h" // pg_round_down() 
#include "userprog/process.h"

struct list frame_table;
// Project 3.1_end
// Project 3.2_anonymous page
struct list_elem* start;
// Project 3.2_end

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	list_init(&frame_table);
	start = list_begin(&frame_table);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable,
                                    vm_initializer *init, void *aux)
{
    // printf("============= VM_TYPE :: %d =============\n", type);
    ASSERT(VM_TYPE(type) != VM_UNINIT)

    struct supplemental_page_table *spt = &thread_current()->spt;

    /* Check wheter the upage is already occupied or not. */
    if (spt_find_page(spt, upage) == NULL)
    {
        // printf("PPPPPPPPPPPPPPPPP\n");
        /* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
        //! ADD: uninit_new
        struct page* page = (struct page*)malloc(sizeof(struct page));

        typedef bool (*initializerFunc)(struct page *, enum vm_type, void *);
        initializerFunc initializer = NULL;

        switch(VM_TYPE(type)){
            case VM_ANON:
                initializer = anon_initializer;
                break;
            case VM_FILE:
                initializer = file_backed_initializer;
                break;
        }
        
        // printf("PPPPPPPPPPP22222222222222\n");
        uninit_new(page, upage, init, type, aux, initializer);

        //! page member 초기화
        page->writable = writable;
        // hex_dump(page->va, page->va, PGSIZE, true);

        /* TODO: Insert the page into the spt. */
        return spt_insert_page(spt, page);
		// Project
    }
err:
    return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	// struct page *page = NULL;
	// malloc을 해야 스레드 이름이 안없어진다.
	// Project 3.1_memory management
	struct page* page = (struct page*)malloc(sizeof(struct page));
	/* TODO: Fill this function. */
	struct hash_elem *e;

	page->va = pg_round_down(va);
	e = hash_find(&spt->pages, &page->hash_elem);
	free(page);
	return e != NULL ? hash_entry (e, struct page, hash_elem) : NULL;
	// return page;
	// Project 3.1_end
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	// int succ = false;
	/* TODO: Fill this function. */

	// Project 3.1_memorty management
	return insert_page(&spt->pages, page);
	// Project 3.1_end
	// return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	
	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	// struct frame *frame = NULL;
	// projec 3.1_memory management
	struct frame *frame = (struct frame*)malloc(sizeof(struct frame));
	/* TODO: Fill this function. */
	frame->kva = palloc_get_page(PAL_USER);
	if(frame->kva == NULL)
	{
		frame = vm_evict_frame();
		frame->page = NULL;
		return frame;
	}
	list_push_back(&frame_table, &frame->frame_elem);
	frame->page = NULL;

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
	// Project 3.3_stack growth
	void *stack_bottom = pg_round_down (addr);
	size_t req_stack_size = USER_STACK - (uintptr_t)stack_bottom;
	if (req_stack_size > (1 << 20)) PANIC("Stack limit exceeded!\n"); // 1MB

	//Alloc page from tested region to previous claimed stack page.
	void *growing_stack_bottom = stack_bottom;
	while ((uintptr_t) growing_stack_bottom < USER_STACK &&
		vm_alloc_page (VM_ANON | VM_MARKER_0, growing_stack_bottom, true)) { // VM_MARKER_0 스탐은 STACK으로 함
		growing_stack_bottom += PGSIZE;
	};
	vm_claim_page(stack_bottom); // Lazy load requested stack page only

	// Project 3.3_end
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
// page fault 발생시 핸들링을 위해 호출 되는 함수
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr,
		bool user, bool write, bool not_present) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	// struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */

	// Project 3.1_memory management
	if(is_kernel_vaddr(addr)) return false;

	void *rsp_stack = is_kernel_vaddr(f->rsp) ? thread_current()->rsp_stack : f->rsp;
	if (not_present){
		if(!vm_claim_page(addr))
		{
			// Project 3.3_stack grow
			// if(rsp_stack - 8 <= addr && USER_STACK - 0x100000 <= addr && addr <= USER_STACK)
			// {
			// 	vm_stack_growth(thread_current()->stack_bottom - PGSIZE);
			// 	return true;
			// }
			// Project 3.3_end
			return false;
		}
		else
			return true;
	}
	return false;
	// Project 3.1_end

	// return vm_do_claim_page (page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	// struct page *page = NULL;
	struct page *page;
	// Project 3.1_memory management
	page = spt_find_page(&thread_current()->spt, va);
	if (page == NULL) return false;
	// Project 3.1_end
	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */

	if(install_page(page->va, frame->kva, page->writable))
	{
		return swap_in(page, frame->kva);
	}
	return false;
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	// Project 3.1 Memory Management
	hash_init(&spt->pages, page_hash, page_less, NULL);
	// Project 3.1 end
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
							  struct supplemental_page_table *src UNUSED) {
	// Project 3.2_anonymous page
	struct hash_iterator i;
	hash_first(&i, &src->pages);
	while(hash_next(&i))
	{
		
		struct page *parent_page = hash_entry(hash_cur(&i), struct page, hash_elem);

		enum vm_type type = page_get_type(parent_page);
		void *upage = parent_page->va;
		bool writable = parent_page->writable;
		vm_initializer *init = parent_page->uninit.init;
		void* aux = parent_page->uninit.aux;

		if(parent_page->uninit.type & VM_MARKER_0)
		{
			setup_stack(&thread_current()->tf);
		}
		else if(parent_page->operations->type == VM_UNINIT)
		{
			if(!vm_alloc_page_with_initializer(type, upage, writable, init, aux))
				return false;
		}
		else
		{ // UNIT이 아니면 spt 추가만
			if(!vm_alloc_page(type,upage, writable))
				return false;
			if(!vm_claim_page(upage))
				return false;
		}
		if(parent_page->operations->type != VM_UNINIT)
		{
			struct page* child_page = spt_find_page(dst,upage);
			memcpy(child_page->frame->kva, parent_page->frame->kva, PGSIZE);
		}
	}
	return true;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	// Project 3.2_anynomous page
	struct hash_iterator i;

	hash_first(&i, &spt->pages);
	while(hash_next(&i))
	{
		struct page *page = hash_entry(hash_cur(&i), struct page, hash_elem);
		if(page->operations->type == VM_FILE)
		{
			do_munmap(page->va);
			//destroy(page);
		}
		//free(page);
	}
	hash_destroy(&spt->pages, spt_destructor);
}
	// Project 3.2_end

// Project 3.1_memory management
// Functions for hash table
// page_hash = vm_hash_func
unsigned
page_hash(const struct hash_elem *p_, void *aux UNUSED)
{
	const struct page *p = hash_entry(p_, struct page, hash_elem);
	return hash_bytes(&p->va, sizeof p->va);
}

// Returns true if page a precedes page b.
// page_less = vm_less_func
bool page_less(const struct hash_elem *a_,
			   const struct hash_elem *b_, void *aux UNUSED)
{
	const struct page *a = hash_entry(a_, struct page, hash_elem);
	const struct page *b = hash_entry(b_, struct page, hash_elem);

	return a->va < b->va;
}

bool insert_page(struct hash *pages, struct page *p)
{
	if (!hash_insert(pages, &p->hash_elem))
		return true;
	else
		return false;
}
// Project 3.1_end


// Project 3.2_anonymous page
void spt_destructor(struct hash_elem *e, void* aux)
{
	const struct page *p = hash_entry(e, struct page, hash_elem);
	free(p);
}
// Project 3.2_end
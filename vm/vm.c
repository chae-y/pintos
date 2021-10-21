/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/vaddr.h"
#include "lib/kernel/hash.h"
#include "userprog/process.h"

//project 9
bool page_hash(const struct hash_elem *p_, void *aux UNUSED);
bool page_less(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED);

//project 9
struct list frame_table;
struct list_elem* start;

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

	//project 9
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
//주어진 유형으로 초기화되지 않은 페이지를 만듭니다.
//unitit페이지의 swap in 핸들러는 자동으로 페이지를 타입에 따라 초기화하고, 주어진 aus롤 init를 호출한다.
//페이지 구조가 있으면 페이지를 프로세스의 보조 페이지 테이블에 삽입하시오.
//vm.h에 정의된 vm_type매크로를 사용하면 편리함
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	//project 10
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */

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

		uninit_new(page, upage, init, type, aux, initializer);
		page->writable = writable;

		/* TODO: Insert the page into the spt. */
		return spt_insert_page(spt, page);
	}
err:
	return false;
}

//project 9
//보충 페이지 테이블에서 알맞은 page를 찾는다.
/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	/* TODO: Fill this function. */
	struct page* page = (struct page*)malloc(sizeof(struct page));

	struct hash_elem *e;

	page->va = pg_round_down(va);//페이지 크기에 맞게

	e = hash_find(&spt->pages, &page->hash_elem);

	free(page);

	if(e == NULL){
		return NULL;
	}

	return hash_entry(e, struct page, hash_elem);
}

//project 9
/* Insert PAGE into spt with validation. */
//주어진 보충 페이지 테이블에 page를 넣습니다.
//이 함수는 주어진 보충 페이지 테이블에 가상주소가 없는지 체크해야합니다.
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	/* TODO: Fill this function. */
	if (hash_insert(&spt->pages, &page->hash_elem) == NULL){
		return true;
	}
	return false;
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

//project 9
/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	swap_out(victim->page);
	return NULL;
}

//project 9
/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
//palloc_get_page를 호출하여 사용자 풀에서 새로운 물리 페이지를 가져옴
//가용자 풀에서 페이지를 성공적으로 받으면 프레임을 할당하고 멤버를 초기화 하고 반환
//vm_get_frame를 구현한 수 이 함수를 통해 모든 사용자 공간 페이지를 할당해야 함
//페이지 할당 오류가 발생 할 경우 현재 스왑을 처리할 필요가 없다
static struct frame *
vm_get_frame (void) {
	struct frame *frame = palloc_get_page(sizeof(struct frame));
	/* TODO: Fill this function. */

	frame->kva = palloc_get_page(PAL_USER);
    if(frame->kva == NULL)
    {
        frame = vm_evict_frame();
        frame->page = NULL;
        return frame;
    }
    list_push_back (&frame_table, &frame->frame_elem);

    frame->page = NULL;


	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;

	if(is_kernel_vaddr(addr))
    {
        return false;
    }

	if (not_present){
        return vm_claim_page(addr);
    }
    
    return false;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

//project 9
//va에 할당하도록 주장
//먼저 페이지를 얻고 페이지와함꼐 vm do claim page호출
/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function */
	page = spt_find_page(&thread_current()->spt, va);
	 if (page == NULL)
        return false;

	return vm_do_claim_page (page);
}

//projrct 9
//물리적 프레임인 페이지를 할당하는 것을 의미
//vm_get_frame을 먼저 호출하여 프레임을 얻는다 그런다음 mmu설정
//즉, 가상주소에서 페이지 테이블의 실제 주소에 매핑을 추가합니다.
//반환값은 작업이 성공했는지 여부를 표시해야합니다
/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */

	if(install_page(page->va, frame->kva, page->writable)) //kva라는 포인터로 일정 메모리를 할당 받은 후 파일을 읽어 kva에 저당하고 이를 va에 연결시켜준다.
    {	//간단히 말해서 page table에 va:kva를 매핑하는 작업(반대는 clear_page())
        return swap_in(page, frame->kva);
    }

	return false;
}

/* Initialize new supplemental page table */
//project 9
//보충 페이지테이블을 초기화 합니다.
//보충 페이지 테이블에 사용할 데이터 구조를 선택 할 수 있습니다.-hash
//함수는 새 프로세스가 시작될 때 그리고 프로세트가 fork될 때 불립니다.
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init(&spt->pages, page_hash, page_less, NULL);
	//해시 테이블을 초기화 해주는 함수
	// spt->pages를 초기화하고, page_hash는 해시값을 구해주는 함수 포인터, page_less는 해시 element들의 크기를 비교해주는 함수포인터(hash_find에서 사용)
}

//project 10
/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
	//! ADD: supplemental_page_table_copy
    // bool success;
    struct hash_iterator i;
    hash_first (&i, &src->pages);
    while (hash_next (&i))
    {
        // struct page *parent_page = (struct page*)malloc(sizeof(struct page));
        struct page *parent_page = hash_entry (hash_cur (&i), struct page, hash_elem);

        // printf("copy 스레드 이름 :: %s\n", thread_name());
        // enum vm_type type = parent_page->operations->type;
        enum vm_type type = page_get_type(parent_page);
        void *upage = parent_page->va;
        bool writable = parent_page->writable;
        vm_initializer *init = parent_page->uninit.init;
        void* aux = parent_page->uninit.aux;

        if (parent_page->uninit.type & VM_MARKER_0)
        {
            setup_stack(&thread_current()->tf);
        }

        else if(parent_page->operations->type == VM_UNINIT)
        {
            if(!vm_alloc_page_with_initializer(type, upage, writable, init, aux))
                return false;
        }

        else
        {   //! UNIT이 아니면 spt 추가만
            if(!vm_alloc_page(type, upage, writable))
                return false;
            if(!vm_claim_page(upage))
                return false;
        }

        if (parent_page->operations->type != VM_UNINIT)
        {   //! UNIT이 아닌 모든 페이지(stack 포함)는 부모의 것을 memcpy
            struct page* child_page = spt_find_page(dst, upage);
            memcpy(child_page->frame->kva, parent_page->frame->kva, PGSIZE);
        }


    }

    return true;
}

//project 10
/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	struct hash_iterator i;

    hash_first (&i, &spt->pages);
    while (hash_next (&i))
    {
        struct page *page = hash_entry (hash_cur (&i), struct page, hash_elem);

        if (page->operations->type == VM_FILE)
        {
            do_munmap(page->va);
            // destroy(page);
        }
        // free(page);
    }
    hash_destroy(&spt->pages, spt_destructor);
}

//project 9
bool page_hash(const struct hash_elem *p_, void *aux UNUSED){
	const struct page *p = hash_entry(p_, struct page, hash_elem);
	return hash_bytes(&p->va, sizeof p->va);
}

//projgect 9
bool page_less(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED){
	const struct page *a_page = hash_entry(a, struct page, hash_elem);
	const struct page *b_page = hash_entry(b, struct page, hash_elem);
	return a_page->va < b_page->va;
}

//project 10
void spt_destructor(struct hash_elem *e, void* aux)
{
    const struct page *p = hash_entry(e, struct page, hash_elem);

    free(p);
}
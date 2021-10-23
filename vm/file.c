/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

//project 11
static struct list mmap_file_list;

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
	//project 11
	list_init (&mmap_file_list);
}

/* Initialize the file backed page */
//파일지원 페이지를 초기화 합니다.
//이함수는 먼저 페이지 ->작업에서 파일지원페이지에 대한 핸들러를 설정한다
//메모리를 지원하는 파일과 같은 페이지 구조에 대한 일부 정보를 업데이트할 수 있습니다.
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;

	return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
//연결된 파일을 닫아 파일 백업 페이지를 파괴합니다.
//file_backed_destroy의 호출자가 이를 처리해야합니다.
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
}

//project 10
/* Do the munmap */
void
do_munmap (void *addr) {
	// if (list_empty (&mmap_file_list)) return;
	// for (struct list_elem* i = list_front (&mmap_file_list); i != list_end (&mmap_file_list); i = list_next (i))
	// {
	// 	struct mmap_file_info* mfi = list_entry (i, struct mmap_file_info, elem);
	// 	if (mfi -> start == (uint64_t) addr){
	// 		for (uint64_t j = (uint64_t)addr; j<= mfi -> end; j += PGSIZE){
	// 			struct page* page = spt_find_page(&thread_current() -> spt, (void*) j);
	// 			spt_remove_page(&thread_current()->spt, page);
	// 		}
	// 		list_remove(&mfi->elem);
	// 		free(mfi);
	// 		return;
	// 	}
	// }
}

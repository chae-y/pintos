/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/vaddr.h"
#include <bitmap.h>

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

//project 10
struct bitmap *swap_table;
const size_t SECTORS_PER_PAGE = PGSIZE / DISK_SECTOR_SIZE; // 2^12 / 512

//project 10, 13
/* Initialize the data for anonymous pages */
//anon페이지 하위시스템에대해 초기화 합니다. 이 함수에서 anon페이지와 관련된 모든것을 설정 할 수 있습니다.
//스왑디스크를 설정해야합니다. 스왑 디스크에서 사용가능한 영역과 사용된 영역을 관리하기 위한 데이터 구조가 필요합니다.
//스왑 영역도  PGSIZE 단뒤로 관리합니다.
void
vm_anon_init (void) { 
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1, 1);
	size_t swap_size = disk_size(swap_disk) / SECTORS_PER_PAGE;
	swap_table = bitmap_create(swap_size);

}

/* Initialize the file mapping */
//page->operations에서 익명페이지에 대한 핸드러를 설정함
//현재 비어있는 구조 체인 anon_page에서 일부 정보를 업데이트해야할 수도 있습니다
//이 함수는 익명페이지의 초기화로 사용됩니다
//스와핑을 지원하려면 anon page에 몇가지 정보를 추가해야합니다.
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;

	return true;
}

//project 13
/* Swap in the page by read contents from the swap disk. */
/*디스크에서 메모리로 데이터 내용을 읽어 스왑 디스크에서 익명페이지로 스왑합니다.
데이터의 위치는 페이지가 스왑아웃될 때 페이지 구조에 스왑 디스크가 저장되어 있어야합니다.
스왑테이블을 업데이트 합니다.*/
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;

	int page_no = anon_page->swap_index;

	if(bitmap_test(swap_table, page_no) == false) //유효한 pageno인지?
		return false;

	for(int i=0; i<SECTORS_PER_PAGE; i++){
		disk_read(swap_disk, page_no * SECTORS_PER_PAGE + i, kva+DISK_SECTOR_SIZE*i);
	}

	bitmap_set(swap_table, page_no, false);

	return true;
}

//project 13
/* Swap out the page by writing contents to the swap disk. */
/* 메모리에서 디스크로 내용을 복사하여 익명페이즈를 스왑 디스크로 교체합니다. 
먼저 스왑 테이블을 사용하여 디스크에서 여유 스왑슬롯을 찾은 다음 데이터 페이지를 슬롯에 복사합니다.
데이터의 위치는 페이지 구조에 저장해야합니다.
디스크에 더이상 여유 슬롯이 없으면 커널을 패닉 상태로 만들수 있습니다.
*/
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	int page_no = bitmap_scan(swap_table,0,1,false);//swaptable에 들어있지않으면

	if(page_no == BITMAP_ERROR)
		return false;

	for(int i=0; i<SECTORS_PER_PAGE; i++){
		disk_write(swap_disk, page_no*SECTORS_PER_PAGE+i, page->va+DISK_SECTOR_SIZE*i);
	}

	bitmap_set(swap_table, page_no, true);
	pml4_clear_page(thread_current()->pml4, page->va);
	// pml4_set_dirty (thread_current()->pml4, page->va, false);//스탐
	page->frame = NULL;//스탐

	anon_page->swap_index = page_no;

	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
//anonymous 페이지가 보유한 리소스를 해제합니다.
//페이지 구조를 명시적으로 해제할 필요는 없으며 호출자가 수행해야합니다.
static void
anon_destroy (struct page *page) {
	//여기도 스탐인듯
	if(page->frame != NULL){
		list_remove(&page->frame->frame_elem);
		free(page->frame);
	}else{
		struct anon_page *anon_page = &page->anon;
		// ASSERT(anon_page->swap_slot_idx != INVALID_SLOT_IDX);
		// bitmap_set(swap_table, anon_page->swap_slot_idx, false);
	}
}

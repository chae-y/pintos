#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);


/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	tid_t tid;
	
	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);

	char *save_ptr;
	strtok_r(file_name, " ", &save_ptr);

	/* Create a new thread to execute FILE_NAME. */
	tid = thread_create (file_name, PRI_DEFAULT, initd, fn_copy);
	if (tid == TID_ERROR)
		palloc_free_page (fn_copy);
	return tid;
}

/* A thread function that launches first user process. */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif
	// Project 3.1_memory management
	process_init();
	// Project 3.1_end
	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t
process_fork (const char *name, struct intr_frame *if_) {
	/* Clone current thread to new thread.*/
	struct thread *cur = thread_current ();
	memcpy(&cur->parent_if, if_, sizeof(struct intr_frame));
	tid_t tid = thread_create (name, PRI_DEFAULT, __do_fork, cur);
	if(tid == TID_ERROR)
		return TID_ERROR;

	struct thread *child = get_child_with_pid(tid);
	sema_down(&child->fork_sema);
	if (child->exit_status == -1)
		return TID_ERROR;
	
	return tid;
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux)
{
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	// false return 하면 go to error들어가짐
	if(is_kernel_vaddr(va)){return true;}
		  
	/* 2. Resolve VA from the parent's page map level 4. */
	parent_page = pml4_get_page (parent->pml4, va);
	if(parent_page==NULL){return false;}
	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */
	newpage = palloc_get_page(PAL_USER);
	if (newpage == NULL)
	{
		printf("[fork-duplicate] failed to palloc new page\n"); // #ifdef DEBUG
		return false;
	}
	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	memcpy(newpage, parent_page, PGSIZE);
	/* pte = parent process */
	writable = is_writable(pte);
	
	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: if fail to insert page, do error handling. */
		printf("\n error at 6. TODO \n");
		return false;
	}
	return true;
}
#endif


/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
struct MapElem
{
	uintptr_t key;
	uintptr_t value;
};

static void
__do_fork (void *aux)
{
	struct intr_frame if_;
	struct thread *parent = (struct thread *) aux;
	struct thread *current = thread_current ();
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	struct intr_frame *parent_if = &parent->parent_if;
	bool succ = true;
	
	/* 1. Read the cpu context to local stack. */
	memcpy (&if_, parent_if, sizeof (struct intr_frame));
	if_.R.rax = 0;  // fork return value for child ??????


	/* 2. Duplicate PT */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;
	
	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/

	if (parent->fdIdx == FDCOUNT_LIMIT)
		goto error;

	/*        자식 프로세스에 부모프로세스 파일 복사       */
	const int MAPLEN = 10;
	struct MapElem map[10]; // key - parent's struct file * , value - child's newly created struct file *
	int dupCount = 0;		// index for filling map

	for (int i = 0; i < FDCOUNT_LIMIT; i++)
	{
		struct file *file = parent->fdTable[i];
		if (file == NULL)
			continue;

		// Project2-extra) linear search on key-pair array
		// If 'file' is already duplicated in child, don't duplicate again but share it
		bool found = false;
		for (int j = 0; j < MAPLEN; j++)
		{
			if (map[j].key == file)
			{
				found = true;
				current->fdTable[i] = map[j].value;
				break;
			}
		}
		if (!found)
		{
			struct file *new_file;
			if (file > 2)
				new_file = file_duplicate(file);
			else
				new_file = file; // 1 STDIN, 2 STDOUT

			current->fdTable[i] = new_file;
			if (dupCount < MAPLEN)
			{
				map[dupCount].key = file;
				map[dupCount].value = new_file;
				dupCount++;
			}
		}
	}
	current->fdIdx = parent->fdIdx;
	
	/*      왜있는걸까 나중에 추가될지도  ???*/
	process_init ();
	
	// 부모 프로세스 깨워줘야됨 이제
	sema_up(&current->fork_sema);
	

	/* Finally, switch to the newly created process. */
	if (succ)
	{
		do_iret (&if_);
	}
		

/* 비정상적인 종료 */
error:
	current->exit_status = TID_ERROR;
	sema_up(&current->fork_sema);
	exit(TID_ERROR);
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
int
process_exec (void *f_name) {
	char *file_name = f_name;
	bool success;

	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current conxtext */
	process_cleanup ();
	// Project 3.1_memory management
	#ifdef VM
	supplemental_page_table_init(&thread_current()->spt);
	#endif
	// Project 3.1_end
	
	/* And then load the binary */
	success = load (file_name, &_if);
	palloc_free_page (file_name);
	/* If load failed, quit. */
	if (!success) {
		return -1;
	}

	/* Start switched process. */
	do_iret (&_if);
	NOT_REACHED ();
}


/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int
process_wait (tid_t child_tid UNUSED) {
	struct thread *curr = thread_current ();
	struct thread *child_th = get_child_with_pid(child_tid);

	if (child_th == NULL)
		return -1;

	sema_down(&child_th->wait_sema);

	int exit_status = child_th->exit_status; // why??

	list_remove(&child_th->child_elem);

	sema_up(&child_th->free_sema);   		 // Why??
	return exit_status;						// return 값을 레지스터에 넣어준다 (wait syscall)
}




/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
	struct thread *cur = thread_current ();
	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */

	for (int i = 0; i < FDCOUNT_LIMIT; i++)
	{
		close(i);
	}
	/* fdtable 할당해준거 free 해줌*/
	palloc_free_multiple(cur->fdTable, FDT_PAGES);

	/* file deny 부분 cur->running에는 실행중이 file의 주소가 저장되있음       */
	/* 때문에 더이상 다른 process(kernel)가 접근 할수있도록 allow해줘야됨*/
	file_close(cur->running);

	process_cleanup ();

	sema_up(&cur->wait_sema);
	sema_down(&cur->free_sema);
	
}

/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr { // Ehdr = ELF header
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type; // 1,2,3,4 각각 재배치, 실행, 공유, 코어 명시
	uint16_t e_machine; // 0x3E : x86-64
	uint32_t e_version; // 오리지널 버전의 ELF인 경우 1로 설정됨
	uint64_t e_entry; // Entry point 메모리 주소
	uint64_t e_phoff; // Program header table offset 시작 포인트
	uint64_t e_shoff; // Section header table offset 시작 포인트
	uint32_t e_flags; 
	uint16_t e_ehsize; // header의 크기
	uint16_t e_phentsize; // Program header table entry 크기
	uint16_t e_phnum; // Program header table entry 개수
	uint16_t e_shentsize; // Section header table entry 크기
	uint16_t e_shnum; // Section header table entry 개수
	uint16_t e_shstrndx; // Sectino header table entry의 인덱스 (section 이름 포함)
};

struct ELF64_PHDR { // Program header table
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;
	char *token, *save_ptr;
	char *argv[64];
	int argc = 0;
	char *file_name_copy[30];
	memcpy(file_name_copy, file_name, strlen(file_name) + 1);
	/*                       file_name 파싱하는 부분           */
	for(token = strtok_r (file_name_copy, " ", &save_ptr); token != NULL; token = strtok_r (NULL, " ", &save_ptr))
	{
		argv[argc] = token;
		argc++;
	}
	/* Allocate and activate page directory. */
	t->pml4 = pml4_create (); // kernel_pool
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());
	/* Open executable file. */
	file = filesys_open (argv[0]);
	if (file == NULL) {
		printf ("load: %s: open failed\n", argv[0]);
		goto done;
	}
	t->running = file;
	file_deny_write(file);
	/* Read and verify executable header. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2 // 1,2,3,4 는 각각 재배치, 실행, 공유, 코어를 명시
			|| ehdr.e_machine != 0x3E // amd64. x86-64
			|| ehdr.e_version != 1  // 오리지널 버전의 ELF인 경우 1
			|| ehdr.e_phentsize != sizeof (struct Phdr) // 프로그램 헤더 테이블 엔트리의 크기
			|| ehdr.e_phnum > 1024) { // 프로그램 헤더 테이블에서 엔트리 개수
		printf ("load: %s: error loading executable\n", argv[0]);
		goto done;
	}
	/* Read program headers. */
	file_ofs = ehdr.e_phoff; // e_phoff : 프로그램 헤더 테이블 시작을 가리킴
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* Set up stack. */
	if (!setup_stack (if_))
		goto done;

	/* Start address. */
	// Stores the executable's entry point into *RIP
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */
	argument_stack(argv, argc, if_);
	
	success = true;
	
	
done:
	/* We arrive here whether the load is successful or not. */
	//file_close (file);
	
	return success;
}


/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL // VA이용 -> PA 확인 잇 kva, 없 NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable)); // pml4의 UPAGE(V) to PF(KPAGE에 의해 확인된)
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

// Project 3.1_memory management
bool
install_page (void *upage, void *kpage, bool writable){
	struct thread *t = thread_current ();

	return (pml4_get_page (t->pml4, upage)== NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
// Project 3.1_end


// Project 3.2_anonymous page
bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
    //! 이게 맞나?; aux[0]을 *file로 casting하고 싶어서, 참조 가능한 이중 void 포인터로((void **)aux) 먼저 캐스팅
	// TODO : page 구조체 member로 넣어놓은 것들 불러오기

    // struct file *file = ((struct box *)aux)->file;
	// off_t ofs = ((struct box*)aux)->ofs;
    // size_t page_read_bytes = ((struct box *)aux)->page_read_bytes;
    // size_t page_zero_bytes = PGSIZE - page_read_bytes;
	struct load_info* li = (struct load_info *) aux;
	if (page == NULL) return false;

    
    // if (file_read (file, page->frame->kva, page_read_bytes) != (int) page_read_bytes) {
    //     palloc_free_page (page->frame->kva);
    //     return false;
    // }
	/* Load this page. */
	if(li->page_read_bytes > 0){
		file_seek (li->file, li->ofs);
		if (file_read (li->file, page->frame->kva, li->page_read_bytes) != (int) li->page_read_bytes) { // fil의
			vm_dealloc_page (page);
			free(li);
			return false;
			}
	}
    // memset (page->frame->kva + page_read_bytes, 0, page_zero_bytes);
	memset (page->va + li->page_read_bytes, 0, li->page_zero_bytes);
    // hex_dump(page->va, page->va, PGSIZE, true);
    file_close(li->file);
	free(li);
    return true;
    //Project 3.2_end
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
// Project 3.1_anonymous page
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	off_t read_ofs = ofs;
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		// TODO: Set up aux to pass information to the lazy_load_segment. */
        //! ADD: aux modified

        // struct box *box = (struct box*)malloc(sizeof(struct box));

        // box->file = file;
        // box->ofs = ofs;
        // box->page_read_bytes = page_read_bytes;

		struct load_info *aux = malloc (sizeof(struct load_info));
		aux->file = file_reopen(file);
		aux->ofs = read_ofs;
		aux->page_read_bytes = page_read_bytes;
		aux->page_zero_bytes = page_zero_bytes;
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, (void*)aux)){
			free(aux);
			return false;
		}
			
		// free(box);
		// hex_dump(page->va, page->va, PGSIZE, true);
		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
        //! ADD : ofs 이동시켜야 함
		// read_ofs += PGSIZE;
		read_ofs += page_read_bytes;
	}
	return true;
}
// Project 3.1_end

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */
	// Project 3.2_anonymous page
	if(vm_alloc_page(VM_ANON | VM_MARKER_0, stack_bottom, 1))
	{
		success = vm_claim_page(stack_bottom);

		if(success){
			if_->rsp = USER_STACK;
			thread_current()->stack_bottom = stack_bottom;
		}
	}
	//project 3.2_end

	return success;
}
#endif /* VM */

/*                      명령어 인자 스택에 쌓음          */
void argument_stack(char **argv, int argc, struct intr_frame *if_)
{
	int length = 0;
	void *rsp = if_->rsp;
	for(int i=argc-1; i>=0; i--)
	{
		/*    인자 마지막에 null 추가해줘야됨       */
		length = strlen(argv[i])+1;
		rsp -= length;
		memcpy(rsp,argv[i],length);
		argv[i] = (uint64_t)rsp;
	}

	/*      word aligh                      */
	while((uint64_t)rsp%8 != 0)
	{
		rsp -= 1;
		*(uint8_t *)rsp = 0;
	}

	for (int i = argc; i >= 0; i--)
	{
		rsp = rsp - 8;

		if (i==argc)
			memset(rsp, 0, sizeof(char*));
		else
			memcpy(rsp, &argv[i], sizeof(char**));
			
	}
	/*        before create fake return  rsi 업데이트         */
	if_->R.rdi = argc;
	if_->R.rsi = rsp;

	rsp -= 8;
	memset(rsp, 0, sizeof(char*));
	if_->rsp = rsp;

	/* Debugging */	
	//printf ("Hey! This is your stack!\n\n\n\n\n\n\n\n");
	//hex_dump(if_->rsp, rsp, USER_STACK-if_->rsp, true);

}


struct thread *get_child_with_pid(int pid)
{
	struct thread *cur = thread_current();
	struct list *child_list = &cur->child_list;

	for (struct list_elem *e = list_begin(child_list); e != list_end(child_list); e = list_next(e))
	{
		struct thread *t = list_entry(e, struct thread, child_elem);
		if (t->tid == pid)
			return t;
	}
	return NULL;
}
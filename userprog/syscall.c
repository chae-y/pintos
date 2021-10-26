#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "threads/palloc.h"
#include "threads/flags.h"
#include "threads/vaddr.h"
#include "userprog/gdt.h"
#include "userprog/process.h"
#include "userprog/syscall.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include <list.h>
#include <stdio.h>
#include <syscall-nr.h>
#include "intrinsic.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);

/* Projects 2 and later. */
void check_address(const uint64_t *);
void halt (void) NO_RETURN;
void exit (int status) NO_RETURN;
tid_t fork(const char *, struct intr_frame *);
int exec (const char *file);
int wait (pid_t);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned length);
int write (int fd, const void *buffer, unsigned length);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);

int dup2(int oldfd, int newfd);

//project 12
void *mmap (void *addr, size_t length, int writable, int fd, off_t offset);
void munmap (void *addr);

int process_add_file (struct file *);
struct file *process_get_file (int);
void process_close_file (int);

//project 12
void check_valid_buffer(void* buffer, unsigned size, void* rsp, bool to_write);

const int STDIN = 1;
const int STDOUT = 2;
struct lock file_lock;

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);
	lock_init(&file_lock);
	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
	lock_init(&filesys_lock);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.

	//project 12
	#ifdef VM
    thread_current()->rsp_stack = f->rsp;
    #endif
	
	switch (f->R.rax)
	{
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		exit(f->R.rdi);
		break;
	case SYS_FORK:
		f->R.rax = fork(f->R.rdi, f);
		break;
	case SYS_EXEC:
		if (exec(f->R.rdi) == -1)
			exit(-1);
		break;
	case SYS_WAIT:
		f->R.rax = process_wait(f->R.rdi);
		break;
	case SYS_CREATE:
		f->R.rax = create(f->R.rdi, f->R.rsi);
		break;
	case SYS_REMOVE:
		f->R.rax = remove(f->R.rdi);
		break;
	case SYS_OPEN:
		f->R.rax = open(f->R.rdi);
		break;
	case SYS_FILESIZE:
		f->R.rax = filesize(f->R.rdi);
		break;
	case SYS_READ:
		//project 12
		check_valid_buffer(f->R.rsi, f->R.rdx, f->rsp, 1);
		f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_WRITE:
		check_valid_buffer(f->R.rsi, f->R.rdx, f->rsp, 0);
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_SEEK:
		seek(f->R.rdi, f->R.rsi);
		break;
	case SYS_TELL:
		f->R.rax = tell(f->R.rdi);
		break;
	case SYS_CLOSE:
		close(f->R.rdi);
		break;
	case SYS_DUP2:
		f->R.rax = dup2(f->R.rdi, f->R.rsi);
		break;
	//ptoject 12
	case SYS_MMAP:
		f->R.rax = mmap(f->R.rdi, f->R.rsi, f->R.rdx, f->R.r10, f->R.r8); // 와 이거 진짜 모르겠다^_^
		break;
	case SYS_MUNMAP:
		munmap(f->R.rdi);
		break;
	default:
		exit(-1);
		break;
	}
}

void check_address(const uint64_t *uaddr)
{
	struct thread *curr = thread_current ();
	if (uaddr == NULL || !(is_user_vaddr(uaddr)) || pml4_get_page(curr->pml4, uaddr) == NULL)
		exit(-1);
}

//project 12
struct page * check_address2(void *addr){
    if (is_kernel_vaddr(addr))
    {
        exit(-1);
    }
    return spt_find_page(&thread_current()->spt, addr);
}

//project 12
void check_valid_buffer(void* buffer, unsigned size, void* rsp, bool to_write)
{
    for (int i = 0; i < size; i++)
    {
        struct page* page = check_address2(buffer + i);
        if(page == NULL)
            exit(-1);
        if(to_write == true && page->writable == false)
            exit(-1);
    }
}

/* PintOS를 종료한다. */
void halt(void)
{
	power_off();
}

/* current thread를 종료한다. exit_status를 기록하고 No return으로 종료한다. */
void exit(int status)
{
	struct thread *curr = thread_current ();
	curr->exit_status = status;

	printf("%s: exit(%d)\n", thread_name (), status);
	thread_exit ();
}

bool create(const char *file, unsigned initial_size)
{
	check_address (file);
	return filesys_create(file, initial_size);
}

bool remove(const char *file)
{
	check_address (file);
	return filesys_remove (file);
}

int wait (tid_t tid)
{
	process_wait (tid);
}

int exec(const char *file_name)
{
	struct thread *curr = thread_current ();
	check_address(file_name);

	// process_exec -> process_cleanup 으로 인해 f->R.rdi 날아감.  때문에 복사 후 다시 넣어줌
	int size = strlen(file_name) + 1;
	char *fn_copy = palloc_get_page(PAL_ZERO);

	if (fn_copy == NULL)
		exit(-1);
	strlcpy(fn_copy, file_name, size);	

	if (process_exec (fn_copy) == -1)
		return -1;
	
	NOT_REACHED();
	return 0;
}

int open (const char *file)
{
	// file이 존재하는지 항상 체크
	check_address(file);
	struct file *file_obj = filesys_open(file);

	if (file_obj == NULL)
		return -1;
	
	int fd = process_add_file(file_obj);

	if (fd == -1)
		file_close(file_obj);
	
	return fd;
}

int filesize (int fd)
{
	struct file *file_obj = process_get_file(fd);
	if (file_obj == NULL)
		return -1;

	return file_length(file_obj);
}

int read (int fd, void *buffer, unsigned size)
{
	check_address(buffer);  /* page fault를 피하기 위해 */
	int ret;
	struct thread *curr = thread_current ();

	
	struct file *file_obj = process_get_file(fd);
	if (file_obj == NULL)
		return -1;

	if (file_obj == 1) {
		if (curr->stdin_count == 0) {
			NOT_REACHED();

		}
		int i;
		unsigned char *buf = buffer;
		for (i = 0; i < size; i++) {
			char c = input_getc();
			*buf++ = c;
			if (c == '\0')
				break;
		}
		return i;
	}

	if (file_obj == 2) {
		return -1;
	}
	
	lock_acquire (&filesys_lock);
	ret = file_read(file_obj, buffer, size);
	lock_release (&filesys_lock);
	
	return ret;
}


int write (int fd, const void *buffer, unsigned size)
{
	check_address(buffer);  /* page fault를 피하기 위해 */
	int ret;
	struct thread *curr = thread_current ();

	struct file *file_obj = process_get_file(fd);
	if (file_obj == NULL)
		return -1;

	if (file_obj == 2) {
		if (curr->stdout_count == 0) {
			NOT_REACHED();
			process_close_file(fd);
			return -1;
		}

		putbuf(buffer, size);
		return size;
	}

	if (file_obj == 1) {
		return -1;
	}
	
	lock_acquire (&filesys_lock);
	ret = file_write(file_obj, buffer, size);
	lock_release (&filesys_lock);
	
	return ret;
}

void seek (int fd, unsigned position)
{
	struct file *file_obj = process_get_file(fd);
	
	if (file_obj <= 2)
		return ;

	file_obj->pos = position;
	//project 12
	file_seek(file_obj, position);
}


unsigned tell (int fd)
{
	struct file *file_obj = process_get_file(fd);

	if (file_obj <= 2)
		return ;

	return file_tell(file_obj);

}

void close (int fd)
{
	struct file *file_obj = process_get_file(fd);
	if (file_obj == NULL)
		return ;

	struct thread *curr = thread_current ();
	
	if (file_obj == 1 || fd == 0)
		curr->stdin_count --;
	
	else if (file_obj == 2 || fd == 1)
		curr->stdout_count --;
	
	
	if (fd <= 1 || file_obj <= 2)
		return;
	process_close_file(fd);

	if (file_obj->dupCount == 0)
		file_close(file_obj);
	else
		file_obj->dupCount --;
}

tid_t fork (const char *thread_name, struct intr_frame *if_)
{
	return process_fork (thread_name, if_);
}

int dup2 (int oldfd, int newfd)
{
	struct file *old_file = process_get_file(oldfd);
	if (old_file == NULL)
		return -1;
	
	struct file *new_file = process_get_file(newfd);
			
	if (oldfd == newfd)
		return newfd;

	struct thread *curr = thread_current ();
	struct file **fdt = curr->fdTable;

	if (old_file == 1)
		curr->stdin_count ++;
	
	else if (old_file == 2)
		curr->stdout_count ++;
	
	else
		old_file->dupCount ++;


	close(newfd);
	fdt[newfd] = old_file;	
	return newfd;
}


int process_add_file (struct file *f)
{
	struct thread *curr = thread_current();
	struct file **fdt = curr->fdTable; // file descriptor table

	while (curr->fdIdx < FDCOUNT_LIMIT && fdt[curr->fdIdx])
		curr->fdIdx++;

	if (curr->fdIdx >= FDCOUNT_LIMIT)
		return -1;

	fdt[curr->fdIdx] = f;
	return curr->fdIdx;
}

struct file *process_get_file (int fd)
{
	struct thread *curr = thread_current ();
	if (fd < 0 || fd >= FDCOUNT_LIMIT)
		return NULL;

	return curr->fdTable[fd];
}

void process_close_file (int fd)
{
	struct thread *curr = thread_current ();

	if (fd < 0 || fd >= FDCOUNT_LIMIT)
		return ;
	
	curr->fdTable[fd] = NULL;
}

//project 12
/*오프셋에서 시작하여 addr에 있는 프로세스의 가상주소공간으로 fd로 열린 파일이 길이 바이트를 매핑합니다.
전체 파일은 addr에서 시작하는 연속적인 가상페이지에 매핑됩니다.
파일길이가 PGSIZE의 배수가 아닌경우 매핑된 최종 페이지의 일부 바이트가 파일 끝을 넘어 삐져나옵니다.
페이지에 오류가 발생하면 이 바이트를 0 으로 설정하고 페이지를 디스크에 다시 쓸 때 버립니다
성공하면 이 함수는 파일이 매핑된 가상주소를 반환합니다.
실패하면 파일을 매핑하는데 유효한 주소가 아닌 null을 반환해야합니다*/
void *mmap (void *addr, size_t length, int writable, int fd, off_t offset)
{
    if(offset % PGSIZE != 0){ //offset 말고 length 아닐까..? 아닌듯
        return NULL;
    }

    if (pg_round_down(addr) != addr || is_kernel_vaddr(addr) || addr == NULL || (long long)length <= 0 || addr == 0)
        return NULL;
    
    if(fd == 0 || fd == 1)
        exit(-1);
    
    //! vm_overlap
    if(spt_find_page(&thread_current()->spt, addr)) // 이미 있으면
        return NULL;

    struct file *target = process_get_file(fd);

    if(target == NULL) //? fd가 0이랑 1인 것도 알아서 걸러준다.!!
        return NULL;

    void * ret = do_mmap(addr, length, writable, target, offset);

    return ret;
}

//project 12
//아직 매핑 해제되지 않은 동일한 프로세스에서 mmap에 대한 이전 호출에서 반환된 가상주소여야 하는 지정된 주소 범위 addr에 대한 매핑을 해제합니다
// addr 주소의 범위에 대한 매핑을 unmap해라
void munmap (void *addr)
{
    do_munmap(addr);
}
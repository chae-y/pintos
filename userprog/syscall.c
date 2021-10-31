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
#include "vm/vm.h"
#include "filesys/directory.h"
#include "filesys/inode.h"

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

void* mmap (void *addr, size_t length, int writable, int fd, off_t offset);
void munmap (void* addr);

bool chdir (const char* name);
bool mkdir (const char* name);
bool readdir (int fd, char* name);
bool isdir (int fd);
int inumber (int fd);
int symlink (const char* target, const char* linkpath);

int process_add_file (struct file *);
struct file *process_get_file (int);
void process_close_file (int);

void check_valid_buffer(void* buffer, unsigned size, void* rsp, bool to_write);
struct page * check_address2(void *addr);

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
	struct thread* curr = thread_current ();
	curr->saved_sp = f->rsp;
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
	//project 3-mmf
	case SYS_MMAP:
		f->R.rax = (uint64_t) mmap ((void*) f->R.rdi, (size_t) f->R.rsi, (int) f->R.rdx, (int) f->R.r10, (off_t) f->R.r8);
		break;
	case SYS_MUNMAP:
		munmap ((void*) f->R.rdi);
		break;
	//project 4 - SASL
	case SYS_CHDIR:
		f->R.rax = chdir((const char*) f->R.rdi);
		break;
	case SYS_MKDIR:
		f->R.rax = mkdir((const char*) f->R.rdi);
		break;
	case SYS_READDIR:
		f->R.rax = readdir((int) f->R.rdi, (char*) f->R.rsi);
		break;
	case SYS_ISDIR:
		f->R.rax = isdir((int) f->R.rdi);
		break;
	case SYS_INUMBER:
		f->R.rax = inumber((int) f->R.rdi);
		break;
	case SYS_SYMLINK:
		f->R.rax = symlink((const char*) f->R.rdi, (const char*) f->R.rsi);
		break;

	NOT_REACHED();
	}
}

void check_address(const uint64_t *uaddr)
{
	struct thread *curr = thread_current ();
	if (uaddr == NULL || !(is_user_vaddr(uaddr)) || pml4_get_page(curr->pml4, uaddr) == NULL || pml4e_walk(curr->pml4, uaddr,0) == NULL)
		exit(-1);
}

struct page * check_address2(void *addr){
    if (is_kernel_vaddr(addr))
    {
        exit(-1);
    }
    return spt_find_page(&thread_current()->spt, addr);
}

void check_valid_buffer(void* buffer, unsigned size, void* rsp, bool to_write)
{
    /* 인자로받은buffer부터buffer + size까지의크기가한페이지의크기를넘을수도있음*/
    /*check_address를이용해서주소의유저영역여부를검사함과동시에vm_entry구조체를얻음*/
    /* 해당주소에대한vm_entry존재여부와vm_entry의writable멤버가true인지검사*/
    /* 위내용을buffer부터buffer + size까지의주소에포함되는vm_entry들에대해적용*/
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
	// check_address(buffer);  /* page fault를 피하기 위해 */
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
	// check_address(buffer);  /* page fault를 피하기 위해 */
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

static void*
mmap (void *addr, size_t length, int writable, int fd, off_t offset){
	//Handle all parameter error and pass it to do_mmap
	if (addr == 0 || (!is_user_vaddr(addr))) return NULL;
	if ((uint64_t)addr % PGSIZE != 0) return NULL; //page fault 시 자름!
	if (offset % PGSIZE != 0) return NULL;
	if ((uint64_t)addr + length == 0) return NULL;
	if (!is_user_vaddr((uint64_t)addr + length)) return NULL;
	for (uint64_t i = (uint64_t) addr; i < (uint64_t) addr + length; i += PGSIZE){
		if (spt_find_page (&thread_current() -> spt, (void*) i)!=NULL) return NULL;
	}
	struct file* file = process_get_file (fd);
	if (file == NULL) return NULL;
	if (file == 1 || file == 2) return NULL;
	if (length == 0) return NULL;
	return do_mmap(addr, length, writable, file, offset);
}

static void
munmap (void* addr){
	do_munmap(addr);
}

/*프로세스의 현재 작업 디렉터리를 상대 또는 절대 디렉터리로 변경합니다. 
성공하면 true를, 실패하면 false를 반환합니다.*/
bool chdir (const char* name){
	if(name == NULL)	return false;

	char *cp_name = (char *)malloc(strlen(name) + 1);
    strlcpy(cp_name, name, strlen(name) + 1);

	struct dir *chdir = NULL;

	if(cp_name[0] == '/'){
		chdir = dir_open_root();
	}else{
		chdir = dir_reopen(thread_current()->cur_dir);
	}

	char *token, *nextToken, *savePtr;
	token = strtok_r(cp_name, "/", &savePtr);

	struct inode *inode = NULL;
	while(token!=NULL){
		//token이름 검색해서 없으면 false
		if(!dir_lookup(chdir, token, &inode)){
			dir_close(chdir);
			return false;
		}

		//위에랑 중복 같음 - chae
		// /* inode가파일일경우NULL 반환*/
        // if (!inode_is_dir(inode))
        // {
        //     dir_close(chdir);
        //     return false;
        // }

		//dir 디렉터리정보를 메모리에서 해지 잘 모르겠어^^
		dir_close(chdir);

		//inode 정보 저장
        chdir = dir_open(inode);

        token = strtok_r(NULL, "/", &savePtr);
	}
	//현재 작업 디렉터리 변경
    dir_close(thread_current()->cur_dir);
    thread_current()->cur_dir = chdir;
    free(cp_name);
    return true;
}

/*상대 또는 절대 디렉터리인 dir이라는 디렉터리를 만듭니다. 
성공하면 true를, 실패하면 false를 반환합니다. 
dir이 이미 존재하거나 dir의 마지막 이름 외에 디렉토리 이름이 이미 존재하지 않는 경우 실패합니다.
즉, mkdir("/a/b/c")은 /a/b가 이미 있고 /a/b/c가 없는 경우에만 성공합니다.*/
bool mkdir (const char* name){
	lock_acquire(&filesys_lock);
    bool tmp = filesys_create_dir(name);
    lock_release(&filesys_lock);
    return tmp;
}

/*디렉토리를 나타내야 하는 파일 설명자 fd에서 디렉토리 항목을 읽습니다. 
성공하면 READDIR_MAX_LEN + 1바이트를 위한 공간이 있어야 하는 이름에 null로 끝나는 파일 이름을 저장하고 true를 반환합니다. 
디렉토리에 항목이 남아 있지 않으면 false를 반환합니다.

. 그리고 ..는 readdir에 의해 반환되어서는 안됩니다. 
디렉토리가 열려 있는 동안 변경되면 일부 항목이 전혀 읽히지 않거나 여러 번 읽는 것이 허용됩니다. 
그렇지 않으면 각 디렉토리 항목을 순서에 관계없이 한 번만 읽어야 합니다.

READDIR_MAX_LEN은 lib/user/syscall.h에 정의되어 있습니다. 
파일 시스템이 기본 파일 시스템보다 긴 파일 이름을 지원하는 경우 이 값을 기본값인 14에서 늘려야 합니다.*/
bool readdir (int fd, char* name){
	if (name == NULL)
        return false;

    /* fd리스트에서fd에대한file정보를얻어옴*/
    struct file *target = process_get_file(fd);
    if (target == NULL)
        return false;

    /* fd의file->inode가디렉터리인지검사*/
    if (!inode_is_dir(file_get_inode(target)))
        return false;

    /* p_file을dir자료구조로포인팅*/
    struct dir *p_file = target;
    if(p_file->pos == 0)
        dir_seek(p_file, 2 * sizeof(struct dir_entry)); //! ".", ".." 제외

    /* 디렉터리의엔트에서“.”,”..” 이름을제외한파일이름을name에저장*/
    bool result = dir_readdir(p_file, name);
	
    return result;
}

/*fd가 디렉토리를 나타내는 경우 true를 반환하고 일반 파일을 나타내는 경우 false를 반환합니다.*/
bool isdir (int fd){
	struct file *target = process_get_file(fd);
    if (target == NULL)
        return false;

    return inode_is_dir(file_get_inode(target));
}

/*일반 파일이나 디렉토리를 나타낼 수 있는 fd와 관련된 inode의 inode 번호를 반환합니다.

inode 번호는 파일이나 디렉토리를 지속적으로 식별합니다. 
파일이 존재하는 동안 고유합니다. 
Pintos에서 inode의 섹터 번호는 inode 번호로 사용하기에 적합합니다.*/
int inumber (int fd){
	struct file *target = process_get_file(fd);
    if (target == NULL)
        return false;

    return inode_get_inumber(file_get_inode(target));
}

/*문자열 target을 포함하는 linkpath라는 심볼릭 링크를 만듭니다. 
성공하면 0이 반환됩니다. 그렇지 않으면 -1이 반환됩니다.*/
int symlink (const char *target, const char *linkpath){
    //! SOFT LINK
    bool success = false;
    char* cp_link = (char *)malloc(strlen(linkpath) + 1);
    strlcpy(cp_link, linkpath, strlen(linkpath) + 1);

    /* cp_name의경로분석*/
    char* file_link = (char *)malloc(strlen(cp_link) + 1);
    struct dir* dir = parse_path(cp_link, file_link);

    cluster_t inode_cluster = fat_create_chain(0);

    //! link file 전용 inode 생성 및 directory에 추가
    success = (dir != NULL
               && link_inode_create(inode_cluster, target)
               && dir_add(dir, file_link, inode_cluster));

    if (!success && inode_cluster != 0)
        fat_remove_chain(inode_cluster, 0);
    
    dir_close(dir);
    free(cp_link);
    free(file_link);

    return success - 1;
}
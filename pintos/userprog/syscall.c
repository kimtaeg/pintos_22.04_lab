#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/init.h"
#include "threads/loader.h"
#include "threads/flags.h"
#include "threads/palloc.h"
#include "userprog/process.h"
#include "userprog/gdt.h"
#include "intrinsic.h"
#include "kernel/console.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "userprog/process.h"

struct lock filesys_lock;

void syscall_entry (void);
void syscall_handler (struct intr_frame *); // register handler

static void halt (void);
static void exit (int status);
static tid_t fork (const char *thread_name, struct intr_frame *f);
static int exec (void *file_name);
static bool create (const char *name, unsigned initial_size);
static bool remove (const char *name);
static int open (const char *file_name);
static int filesize(int value);
static int read (int fd, void *buffer, unsigned size);
static int write (int fd, const void *buffer, unsigned size);
static void seek (int fd, unsigned position);
static unsigned tell (int fd);
static void close (int value);

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

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
	lock_init(&filesys_lock);
}

static void is_kernel_range(uint64_t addr, size_t size)
{
	if (size == 0)
	{
		uint64_t p = addr;

		while (1)
		{
			if (is_kernel_vaddr(p))
			{
				exit(-1);
			}

			if (p == addr || pg_ofs(p) == 0)
			{
				if (pml4_get_page(thread_current()->pml4, p) == NULL)
				{
					exit(-1);
				}
			}

			if (*(unsigned char *)p == '\0')
			{
				break;
			}

			p += 1;
		}
	}
	else
	{
		// addr + size = end_addr
		//  end_addr in kernel range? exit(-1).
		if (is_kernel_vaddr(addr) || is_kernel_vaddr(addr + size))
		{
			exit(-1);
		}
	}
}

static void crosses_page_boundary(uint64_t addr, size_t size)
{

	uint64_t end_addr = addr + size;
	uint64_t start_pg_no = pg_no(pg_round_down(addr));
	uint64_t end_pg_no = pg_no(pg_round_down(end_addr));

	struct thread *t = thread_current();
	uint64_t *pml4 = t->pml4;

	if (start_pg_no == end_pg_no) // 시작 페이지 번호와 끝 페이지 번호가 같은 경우.
	{
		if (pml4_get_page(pml4, pg_addr(start_pg_no)) == NULL)// 페이지 번호를 페이지 주소로 변환하여 매핑 검증.
		{
			exit(-1);
		}
	}
	else
	{
		while (start_pg_no < end_pg_no) // 시작 페이지 번호보다 끝 페이지 번호가 큰 경우.
		{
			if (pml4_get_page(pml4, pg_addr(start_pg_no)) == NULL)// 페이지 번호를 페이지 주소로 변환하여 매핑 검증.
			{
				exit(-1);
			}
			start_pg_no += 1;// 시작 페이지 번호와 끝 페이지 번호 사이에 있는 모든 페이지를 하나씩 매핑 검증한다.
		}
	}
}

static void check_address_range(uint64_t addr, size_t size)
{
	is_kernel_range(addr, size);
	crosses_page_boundary(addr, size);
}

static void isAddressValid(struct intr_frame *f UNUSED)
{
	switch (f->R.rax)
	{
	case SYS_FORK:
		check_address_range(f->R.rdi, 0);
		// pid_t fork (const char *thread_name);
		break;
	case SYS_EXEC:
		check_address_range(f->R.rdi, 0);
		// int exec (const char *file);
		break;
	case SYS_CREATE:
		check_address_range(f->R.rdi, 0);
		// bool create (const char *file, unsigned initial_size);
		break;
	case SYS_REMOVE:
		check_address_range(f->R.rdi, 0);
		// bool remove (const char *file);
		break;
	case SYS_OPEN:
		check_address_range(f->R.rdi, 0);
		// int open (const char *file);
		break;
	case SYS_READ:
		check_address_range(f->R.rsi, f->R.rdx);
		// int read (int fd, void *buffer, unsigned length);
		break;
	case SYS_WRITE:
		check_address_range(f->R.rsi, f->R.rdx);
		// int write (int fd, const void *buffer, unsigned length);
		break;
	case SYS_MMAP:
		check_address_range(f->R.rdi, f->R.rsi);
		// void *mmap (void *addr, size_t length, int writable, int fd, off_t offset);
		break;
	case SYS_MUNMAP:
		check_address_range(f->R.rdi, sizeof(char));
		// void munmap (void *addr);
		break;
	case SYS_CHDIR:
		check_address_range(f->R.rdi, 0);
		// bool chdir (const char *dir);
		break;
	case SYS_MKDIR:
		check_address_range(f->R.rdi, 0);
		// bool mkdir (const char *dir);
		break;
	case SYS_SYMLINK:
		check_address_range(f->R.rdi, 0);
		check_address_range(f->R.rsi, 0);
		// int symlink (const char* target, const char* linkpath);
		break;

	default:
		break;
	}
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	isAddressValid(f);
	switch (f->R.rax)
	{
	// power_off 호출
	case SYS_HALT:
		halt 	();
		break;
	
	case SYS_EXIT:
		exit ((int) f->R.rdi);
		break;

	case SYS_FORK:
		f->R.rax = fork ((const char *) f->R.rdi, f);
		break;

	case SYS_EXEC:
		exec ((void *) f->R.rdi);
		break;

	case SYS_WAIT:
		f->R.rax = process_wait (f->R.rdi);
		break;

	case SYS_CREATE:
		f->R.rax = create ((char *) f->R.rdi, (unsigned) f->R.rsi);
		break;

	case SYS_REMOVE:
		f->R.rax = remove ((char *) f->R.rdi);
		break;

	case SYS_OPEN:
		f->R.rax = open (f->R.rdi);
		break;

	case SYS_FILESIZE:
		f->R.rax = filesize ((int)f->R.rdi);
		break;

	case SYS_READ:
        f->R.rax = read ((int) f->R.rdi, (void *) f->R.rsi, (unsigned) f->R.rdx);
        break;

	case SYS_WRITE:
		f->R.rax = write ((int) f->R.rdi, (void *) f->R.rsi, (unsigned) f->R.rdx);
		break;

	case SYS_SEEK:
		seek ((int) f->R.rdi, (unsigned) f->R.rsi);
		break;

	case SYS_TELL:
		f->R.rax = tell ((int) f->R.rdi);
		break;

	case SYS_CLOSE:
		close ((int)f->R.rdi);
		break;

	default:
		exit(-1);
		break;
	}
	// printf ("system call!\n");
	// thread_exit ();
}

static void
halt(void){
	power_off();
}

static void
exit (int status){
	// 현재 스레드에서 status 저장
	struct thread *cur = thread_current();
	cur -> exit_status = status;
	thread_exit();
}

static int
exec(void *file_name){
	char *fn_copy;

	fn_copy = palloc_get_page (0);
	if(fn_copy == NULL)
		exit(-1);

	strlcpy(fn_copy, file_name, PGSIZE);

	process_exec(fn_copy);

	exit(-1);
}

static bool
create (const char *name, unsigned initial_size){
	lock_acquire(&filesys_lock);
	bool res = filesys_create(name, initial_size);
	lock_release(&filesys_lock);
	return res;
}

static bool
remove (const char *name){
	lock_acquire(&filesys_lock);
	bool res = filesys_remove(name);
	lock_release(&filesys_lock);
	return res;
}

static int
write (int fd, const void *buffer, unsigned size){
	if(fd==1){
		// 화면에 한번에 출력
		putbuf(buffer, size);
		// 쓴 바이트 반환
		return size;
	}
	else if(fd >= 2){
		// fd로 fd_row 찾기
		struct fd_row *row = fd_row_find(fd);
		// 못찾았으면
		if(row==NULL){
			return -1;
		}
		// 찾았으면
		return file_write(row->file, buffer, size);
	}
	
	return -1;
}

static int
open(const char *file_name)
{
	struct file *file;
	struct fd_row *fd_row;
	struct thread *t = thread_current();
	// filesys_open을 호출하여 file_name 인자에 맞는 inode를 탐색.
	file = filesys_open(file_name);
	// return이 NULL인지 검사.
	if (file == NULL)
		return -1;
	// fd_row_init을 통해 return값과 *file이 매핑된 값을 제작.
	struct fd_row *row = fd_row_init(t->next_fd, file);
	// 칸을 못 만들면 열어 둔 파일도 닫기
	if (row == NULL) {
		file_close(file);
		return -1;
	}
	t->next_fd++;
	// 제작된 fd_row를 curr->fd_table_list에다 삽입.
	list_push_back(&t->fd_table_list, &row->elem);
	// mapping한 return 값 int value 출력.
	return row->value;
}

static void
close(int value)
{
	struct fd_row *fd_row = fd_row_find(value);
	if(fd_row == NULL){
		return;
	}
	file_close(fd_row->file);
	list_remove(&fd_row->elem);
	fd_row_remove(fd_row);
}

static int
filesize(int value){
	struct fd_row *fd_row = fd_row_find(value);
	if(fd_row == NULL){
		return -1;
	}

	off_t size = file_length(fd_row->file);

	return size;
}

read (int fd, void *buffer, unsigned size){
	uint8_t *buf = (uint8_t *) buffer;

	if(fd == 0){
			for(unsigned i=0; i<size; i++){
					// 키보드에서 글자 하나 받아서 i번째 칸에 넣기
					buf[i] = input_getc();
			}
			return size;
	}
	
	else if(fd >= 2){
		// fd로 fd_row 찾기
		struct fd_row *row = fd_row_find(fd);
		// 못찾았으면
		if(row==NULL){
			return -1;
		}
		// 찾았으면
		return file_read(row->file, buf, size);
	}

  return -1;
}

static tid_t 
fork (const char *thread_name, 	struct intr_frame *f){
	return process_fork(thread_name, f);
}

static void seek (int fd, unsigned position){
	if (fd_row_find(fd) != NULL) {
		lock_acquire(&filesys_lock);
		file_seek(fd_row_find(fd)->file, position);
		lock_release(&filesys_lock);
	}else {
		return;
	}
}

static unsigned tell (int fd){
	if (fd_row_find(fd) != NULL) {
		lock_acquire(&filesys_lock);
		int res = file_tell(fd_row_find(fd)->file);
		lock_release(&filesys_lock);
		return res;
	}else {
		return (unsigned) -1;
	}
}
#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *); // register handler

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
				sys_exit(-1);
			}

			if (p == addr || pg_ofs(p) == 0)
			{
				if (pml4_get_page(thread_current()->pml4, p) == NULL)
				{
					sys_exit(-1);
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
			sys_exit(-1);
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
			sys_exit(-1);
		}
	}
	else
	{
		while (start_pg_no < end_pg_no) // 시작 페이지 번호보다 끝 페이지 번호가 큰 경우.
		{
			if (pml4_get_page(pml4, pg_addr(start_pg_no)) == NULL)// 페이지 번호를 페이지 주소로 변환하여 매핑 검증.
			{
				sys_exit(-1);
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
	printf ("system call!\n");
	thread_exit ();
}

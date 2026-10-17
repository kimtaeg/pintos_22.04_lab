#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/init.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "kernel/console.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

static void halt (void);
static void exit (int status);
static int write (int fd, const void *buffer, unsigned size);

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

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	switch (f->R.rax)
	{
	// power_off 호출
	case SYS_HALT:
		halt();
		break;
	
	case SYS_EXIT:
		exit ((int) f->R.rdi);
		break;

	case SYS_FORK:

	case SYS_EXEC:

	case SYS_WAIT:

	case SYS_CREATE:

	case SYS_REMOVE:

	case SYS_OPEN:

	case SYS_FILESIZE:

	case SYS_READ:

	case SYS_WRITE:
		f->R.rax = write ((int) f->R.rdi, (void *) f->R.rsi, (unsigned) f->R.rdx);
		break;

	case SYS_SEEK:

	case SYS_TELL:

	case SYS_CLOSE:

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
	// exit(status) 출력
	cur -> exit_status = status;
	printf ("%s: exit(%d)\n", cur->name, status);

	thread_exit();
}

static int
write (int fd, const void *buffer, unsigned size){
	// 버퍼 유저 포인터 검증

	if(fd==1){
		// 화면에 한번에 출력
		putbuf(buffer, size);
		// 쓴 바이트 반환
		return size;
	}
	
	// fd가 0,2일 경우
	return -1;
}
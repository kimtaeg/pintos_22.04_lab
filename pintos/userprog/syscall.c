#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "threads/synch.h"
#include "filesys/file.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "devices/input.h"
#include "filesys/filesys.h"
#include "threads/palloc.h"
#include "vm/vm.h"



void syscall_entry (void);
void syscall_handler (struct intr_frame *);

struct lock file_lock; 
static void check_buffer (const void *buffer, unsigned size, bool writeable);
static bool check_address(const char *file);
static struct file *fd_to_file (int fd);
static bool check_file(const char *file);
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
	lock_init(&file_lock); // 초기화, 안하면 이상한 값 들어옴

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}
/* The main system call interface */
struct intr_frame *f_data; // 프레임을 집어 넣어야 하는데 인자 개수가 f->R.rdi 하나로 제한되어 전역 변수 선언하여 집어넣음
void
syscall_handler (struct intr_frame *f) {
	// 커널 내에서의 페이지 폴트 시 rsp가 망가지는 것을 대비
	// sel_ucseg 유저 모드를 나타내는 상수
	if(f->cs == SEL_UCSEG)
		thread_current()->user_rsp = f->rsp;

	switch (f->R.rax) {
		case SYS_MUNMAP:
			munmap((void *) f->R.rdi);
			break;

		case SYS_HALT:
			halt ();
			break;

		case SYS_EXIT:
			exit((int) f->R.rdi);
			break;

		case SYS_FORK:{
			f_data = f;
			f->R.rax = process_fork (f->R.rdi, f);
			break;
		}
		case SYS_EXEC:
			exec((const char *) f->R.rdi);
			break;

		case SYS_WAIT:
			f->R.rax = wait((pid_t) f->R.rdi);
			break;

		case SYS_CREATE:
			f->R.rax = create((const char *) f->R.rdi, (unsigned) f->R.rsi);
			break;

		case SYS_REMOVE:
			f->R.rax = remove((const char *) f->R.rdi);
			break;

		case SYS_OPEN:
			f->R.rax = open((const char *) f->R.rdi);
			break;

		case SYS_FILESIZE:
			f->R.rax = filesize((int) f->R.rdi);
			break;

		case SYS_READ:
			f->R.rax = read((int) f->R.rdi, (void *) f->R.rsi, (unsigned) f->R.rdx);
			break;

		case SYS_WRITE:
			f->R.rax = write((int) f->R.rdi, (const void *) f->R.rsi, (unsigned) f->R.rdx);
			break;

		case SYS_SEEK:
			seek((int) f->R.rdi, (unsigned) f->R.rsi);
			break;

		case SYS_TELL:
			f->R.rax = tell((int) f->R.rdi);
			break;

		case SYS_CLOSE:
			close((int) f->R.rdi);
			break;

		case SYS_DUP2:
			f->R.rax = dup2((int)f->R.rdi, (int)f->R.rsi);
			break;

		case SYS_MMAP: {
			int fd = f->R.r10;
			struct thread *t = thread_current ();
			struct file *file = NULL;

			if (fd >= 0 && fd < FILE_MAX && t->fdt != NULL) {
				struct file_descriptor *desc = t->fdt[fd];
				if (desc != NULL && desc->fT == FD_FILE)   /* stdin/stdout 제외 */
					file = desc->file;
			}

			if (file == NULL) {
				f->R.rax = 0;           /* MAP_FAILED */
				break;
			}
			f->R.rax = (uint64_t) do_mmap ((void *) f->R.rdi, f->R.rsi, f->R.rdx, file, f->R.r8);
			break;
		}

		default:
			exit(-1);
			break;
	}
}




static bool check_address(const char *file)
{	
	if (file == NULL) return false;
	#ifdef USERPROG
		struct thread *t = thread_current();
		if (!is_user_vaddr(file)) return false;
		if (pml4_get_page(t->pml4, file) == NULL) return false;
		return true;
	#endif
}

void
halt (void){
	/*
	pintos 종료 시, power_off() (에 선언된 src/include/threads/init.h)를 호출 해야 함
	이 방법은 교착 상태 등에 대한 일부 정보를 잃게 됨
	거의 사용하지 않는 것이 좋음
	*/
	power_off();
}

void
exit (int status){
	/*
	현재 사용자 프로그램 종료
	status 커널로 돌아감 
	프로세스의 부모 wait 프로세스가 해당 프로세스에 대해 종료를 요청한 경우
	이 상태가 반환
	일반적으로 0은 status 성공
	0이 아닌 값은 오류를 나타냄
	*/
	struct thread *t = thread_current();
	t->exit_status = status;
	thread_exit();
}

// pid_t
// fork (const char *thread_name){
// 	return process_fork(thread_name, if);

// }

int
exec (const char *cmd_line){
	/*
	프로세스를 지정된 이름의 실행 파일로 변경
	지정된 인수를 전달할 수 있음
	이 함수는 성공 반환 X
	프로그램이 로드 되거나 실행 되지 않으면 프로세스 종료 : -1
	함수는 호출한 스레드의 이름은 변경하지 않음
	파일 디스크립터는 호출 후에도 계속 열려 있음
 	*/
	uint8_t *cmd_p = cmd_line;
	if (!is_user_vaddr (cmd_p) || cmd_p == NULL){
		return true;
	}

	uint64_t *pml4 = thread_current ()->pml4;
	if (pml4_get_page (pml4, cmd_p) == NULL){
			exit (-1);
	}

	
	char *cmd_cpy = palloc_get_page(0);

	if(cmd_cpy == NULL) exit(-1);

	size_t len = strlcpy(cmd_cpy, cmd_line, PGSIZE);

	if(len >= PGSIZE){
		palloc_free_page(cmd_cpy);
		exit(-1);
	}

	if(process_exec(cmd_cpy) < 0) exit(-1);

	NOT_REACHED();
}

int
wait (pid_t pid){
	int result = process_wait(pid);
	return result;
}	

bool
create (const char *file, unsigned initial_size){
	if (!check_address(file))
	{
		exit(-1); 
	}
	lock_acquire(&file_lock);
	bool success = filesys_create(file, initial_size);
	lock_release(&file_lock);
	return success;
}

bool
remove (const char *file){
	if (!check_address(file))
	{
		exit(-1); 
	}
	lock_acquire(&file_lock);
	bool success = filesys_remove(file);;
	lock_release(&file_lock);
	return success; 
}

int
open (const char *file){
	struct thread *t = thread_current();
	if(t->fdt == NULL) return -1; /*fdt 초기화 안됨*/

	if(check_file(file)){
		return -1;
	}
	

	lock_acquire (&file_lock);
	struct file *f = filesys_open (file);
	lock_release (&file_lock);
	if(f == NULL) return -1;

	/*빈 0,1은 쓰니까 스킵*/
	for(int i = 0; i<FILE_MAX; i++){
		if(t->fdt[i] == NULL){
			struct file_descriptor *desc = malloc(sizeof(struct file_descriptor));

            if (desc == NULL) {
				lock_acquire (&file_lock);
				file_close (f);
				lock_release (&file_lock);
                return -1;
            }

            desc->file = f;
            desc->fT = FD_FILE;
            desc->share = 1;

            t->fdt[i] = desc;
            return i;
		}
	}

	/*파일 저장소 없음*/
	lock_acquire (&file_lock);
	file_close (f);
	lock_release (&file_lock);
	return -1;
}

int
filesize (int fd){
	if (fd < 0 || fd >= FILE_MAX)
		return -1;

	struct file_descriptor *file_desc = thread_current ()->fdt[fd];

	if (file_desc == NULL || file_desc->fT != FD_FILE || file_desc->file == NULL)
		return -1;

	lock_acquire (&file_lock);
	int size = (int) file_length (file_desc->file);
	lock_release (&file_lock);
	return size;
}

int
read (int fd, void *buffer, unsigned size) {
    check_buffer(buffer, size, true);

    struct thread *t = thread_current();

    /* FD 범위 검사 */
    if (fd < 0 || fd >= FILE_MAX)
        return -1;

    /* FDT 및 FD 유효성 검사 */
    if (t->fdt == NULL || t->fdt[fd] == NULL)
        return -1;

    struct file_descriptor *file_des = t->fdt[fd];

    switch (file_des->fT) {
        case FD_STDIN: {
            char *buf = (char *) buffer;

            for (unsigned i = 0; i < size; i++) {
                buf[i] = input_getc();
            }

            return size;
        }

        case FD_STDOUT:
            return -1;

        case FD_FILE: {
            if (file_des->file == NULL)
                return -1;

            lock_acquire(&file_lock);

            int len = file_read(file_des->file, buffer, size);

            lock_release(&file_lock);

            return len;
        }

        default:
            return -1;
    }
}

int
write (int fd, const void *buffer, unsigned size){
	check_buffer(buffer, size, false);

	struct thread *t = thread_current();

	if (fd < 0 || fd >= FILE_MAX) return -1;
	if(t->fdt == NULL) return -1;

	struct file_descriptor *file_des = t->fdt[fd];
	if(file_des == NULL) return -1;
	switch (file_des->fT)
	{
	case FD_STDOUT:{
		putbuf(buffer, size); // buffer에서 n개의 문자를 콘솔에 출력하는 함수
		return size;
	}
	case FD_STDIN:
		return -1;
	case FD_FILE:{
		if (file_des->file == NULL) return -1;
		lock_acquire (&file_lock);
		int written = file_write(file_des->file, buffer, size); // file 사용 함수, file구조체 주소로 넘겨줘야 함
		lock_release (&file_lock);
		return written;
	}
	default:
		return -1;
	}
}

void
seek (int fd, unsigned position){
	if(fd < 0 || fd >= FILE_MAX) return;
	struct thread *t = thread_current();
	struct file *f;
	struct file_descriptor *file_desc = t->fdt[fd];

	if (file_desc == NULL || file_desc->fT != FD_FILE || file_desc->file == NULL)
		return ;

	f = file_desc -> file;

	if(f != NULL) file_seek(f, (off_t)position);
}

unsigned
tell (int fd){
	if(fd < 0 || fd >= FILE_MAX) return (unsigned)-1;
	struct thread *t = thread_current();
	struct file *f; 
	struct file_descriptor *file_desc = thread_current ()->fdt[fd];

	if (file_desc == NULL || file_desc->fT != FD_FILE || file_desc->file == NULL)
		return (unsigned)-1;

	f = file_desc->file;

	if(f != NULL){
		return (unsigned)file_tell(f);
	}
	return (unsigned)-1;
}

void
close (int fd){
	if(fd < 0 || fd >= FILE_MAX) return;
	struct thread *t = thread_current();

	if(t->fdt == NULL) return;

	struct file_descriptor *file_des = t->fdt[fd];
	if(file_des == NULL) return;

	if(file_des->share > 1){
		file_des->share--;
		t->fdt[fd] = NULL;
	}else{
		if(file_des->fT == FD_FILE){
			lock_acquire (&file_lock);
			file_close (file_des->file);
			lock_release (&file_lock);
		}
		free(file_des);
		t->fdt[fd] = NULL;
	}
	return;
}

static void
check_buffer (const void *buffer, unsigned size, bool writeable) {
	if (buffer == NULL)          // 버퍼 포인터가 NULL이면 잘못된 접근이므로 종료
		exit (-1);

	if (size == 0)               // 크기가 0이면 실제로 접근할 메모리가 없으므로 검사할 필요 없음
		return;

	const uint8_t *start = buffer;            // 바이트 단위로 계산하기 위해 uint8_t 포인터로 변환
	const uint8_t *end = start + size - 1;    // 버퍼의 마지막 바이트 주소
	
	if (end < start)             // start + size가 주소 공간 끝을 넘어 0 근처로 되돌아간 경우(오버플로우) - 종료
		exit (-1); // uint8_t * : 64비트 (부호x) => 최대값 끝나면 0으로 감

	uint64_t *pml4 = thread_current ()->pml4; // 현재 프로세스의 페이지 테이블(pml4)을 가져옴

	/* 
	버퍼가 여러 페이지에 걸쳐 있을 수 있으므로,
	start가 속한 페이지의 시작 주소부터 end가 속한 페이지까지 한 페이지씩 검사
	*/
	struct thread *t = thread_current ();
	void *rsp = t->user_rsp;

	for (const uint8_t *p = pg_round_down (start); p <= end; p += PGSIZE) { // pg_round_down 사용시 주소를 그 주소가 속한 페이지의 시작 주소로 내림 
		if (!is_user_vaddr (p)){             // 페이지가 사용자 영역이 아니면 커널 메모리 침범이므로 종료
			exit (-1);
		}
		// spt에 있으면 아직 안 올라왔어도 폴트 핸들러가 처리
		struct page *page = spt_find_page (&t->spt, (void *) p);
		if (page != NULL) {
			if (writeable && !page->rw)   // read는 버퍼에 써야 하므로 쓰기 권한 필요
				exit (-1);
			continue;
		}

		// spt에 없으면 스택 확장 가능한 주소만 허용 
		const uint8_t *addr = (p < start) ? start : p;   // 첫 페이지는 pg_round_down 때문에 실제 시작 주소로 검사
		if(!(addr<USER_STACK && addr >= rsp - 8 && addr >= (USER_STACK - (1<<20)))){
			exit (-1);
		}	
	}
}

static struct file *
fd_to_file (int fd) {
	return thread_current ()->fdt[fd]; // fdt-> 파일디스크립터 테이블이고 fd번호를 인덱스로 file 구조체를 찾는 부분
}

static bool
check_file(const char *file){
	if (!is_user_vaddr (file) || file == NULL){
		return true;
	}
	uint64_t *pml4 = thread_current ()->pml4;
	if (pml4_get_page (pml4, file) == NULL){
			exit (-1);
		}
	return false;
}



int dup2(int oldfd, int newfd){
	struct thread *t = thread_current();
	struct file_descriptor** c_fdt = t->fdt;

	if(c_fdt == NULL) return -1;

	if((oldfd < 0 || oldfd >=FILE_MAX) ||
	(newfd < 0 || newfd >=FILE_MAX))  
	return -1;

	if(c_fdt[oldfd] == NULL) return -1;

	if(c_fdt[oldfd] == c_fdt[newfd]) return newfd;

	if(c_fdt[newfd]!=NULL){
		if(c_fdt[newfd]->share == 1){
			if(c_fdt[newfd]->fT == FD_FILE){
				file_close(c_fdt[newfd]->file);
			}	
			free(c_fdt[newfd]);
			}else{
				c_fdt[newfd]->share--;
			}

	}

	c_fdt[newfd] = c_fdt[oldfd];
	c_fdt[newfd]->share++;

	return newfd;
}

void
munmap(void *addr){
	if ((addr == NULL) || (!is_user_vaddr(addr))) return exit(-1);
	return do_munmap(addr);
}
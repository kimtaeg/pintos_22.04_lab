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
#include "devices/timer.h" // 임시 include
#include "threads/synch.h"
#include "lib/kernel/list.h"
#include "threads/malloc.h"
#include "vm/vm.h"
#ifdef VM
#include "vm/vm.h"
#endif

#define USERPROG 1
static bool setup_stack (struct intr_frame *if_);
static bool lazy_load_segment (struct page *page, void *aux);
static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);
static bool install_page (void *upage, void *kpage, bool writable);

struct 
fork_info{
	struct thread *parent;
	struct intr_frame parent_if_;
	struct semaphore sema;
	bool success;
};

/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/*argument passing to register*/
static bool setup_arguments(
    struct intr_frame *if_,
    int argc,
    char **argv
);

static void
stack_push(uint64_t *rsp, const void *data, size_t size);

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

    char *buffer;
    int word_cnt = 0;

    for (int i = 0; file_name[i] != ' ' && file_name[i] != '\0'; i++) {
        word_cnt++;
    }

    buffer = malloc(word_cnt+1);
    if(buffer == NULL){
        palloc_free_page (fn_copy);
        return TID_ERROR;
    }

    strlcpy(buffer, fn_copy, word_cnt+1);



    /* Create a new thread to execute FILE_NAME. */

    tid = thread_create (buffer, PRI_DEFAULT, initd, fn_copy);
    if (tid == TID_ERROR){
        free(buffer);
        palloc_free_page (fn_copy);
		return TID_ERROR;
    }

    free(buffer);
    return tid;
}

/* A thread function that launches first user process. */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	process_init ();

	if(fd_init())
	{
		/*초기화 실패하면?*/
	}


	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

int
fd_init(){
	struct thread *t = thread_current();
	struct file_descriptor **fd = calloc(FILE_MAX, sizeof(struct file_descriptor *));
	if(fd == NULL) return -1;

	fd[0] = malloc(sizeof(struct file_descriptor));
	if(fd[0] == NULL){
		free(fd);
		return -1;
	} 
	fd[0]->fT = FD_STDIN;
	fd[0]->file = NULL;
	fd[0]->share = 1;	
	
	fd[1] = malloc(sizeof(struct file_descriptor));
	if(fd[1] == NULL){
		free(fd[0]);
		free(fd);
		return -1;
	} 
	fd[1]->fT = FD_STDOUT;
	fd[1]->file = NULL;
	fd[1]->share = 1;

	t->fdt = fd;
	return 0;
}



/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) {
	/* Clone current thread to new thread.*/
	struct fork_info* info = malloc(sizeof(struct fork_info));
	if(info == NULL) return TID_ERROR;//malloc 예외처리
	info->success = false;
	
	sema_init(&info->sema, 0);

	info->parent = thread_current();
	memcpy(&(info->parent_if_), if_, sizeof(struct intr_frame));

	tid_t result = thread_create (name, PRI_DEFAULT, __do_fork, info);
	if(result == TID_ERROR){
		free(info);
		return TID_ERROR;
	}
	sema_down(&info->sema);
	if(!info->success){
		free(info);
		return TID_ERROR;
	}
	free(info);
	return result;
}


#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	if(is_kern_pte(pte)) return true;

	/* 2. Resolve VA from the parent's page map level 4. */
	parent_page = pml4_get_page (parent->pml4, va);
	if(parent_page == NULL) return false;

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */
	newpage = palloc_get_page(PAL_USER);
	if(!newpage) return false;
	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */

	memcpy(newpage, parent_page, PGSIZE);
	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	writable = is_writable(pte);

	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		palloc_free_page(newpage);
		return false;
	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void
__do_fork (void *aux) {
	struct intr_frame if_;
	struct fork_info * info = aux;
	struct thread *parent = info->parent;
	struct thread *current = thread_current ();
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	bool succ = true;

	/* 1. Read the cpu context to local stack. */
	memcpy (&if_, &info->parent_if_, sizeof (struct intr_frame));
	

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

	// if(fd_init()) goto error; //free process 종료시 해줘야 함

	/* 자식의 빈 FDT 배열 생성 */
	current->fdt = calloc(FILE_MAX, sizeof(struct file_descriptor *));
	if (current->fdt == NULL)
		goto error;

	if (parent->fdt == NULL)
		goto error;

	/* 부모 FDT 복제 */
	for (int i = 0; i < FILE_MAX; i++) {
		if (parent->fdt[i] == NULL)
			continue;

		int fd = find_back(parent->fdt, parent->fdt[i], i);

		/* 이미 복제한 공유 객체라면 재사용 */
		if (fd != -1) {
			current->fdt[i] = current->fdt[fd];
			current->fdt[i]->share++;
			continue;
		}

		/* 처음 등장한 객체라면 새로 생성 */
		struct file_descriptor *new_desc =
			calloc(1, sizeof(struct file_descriptor));

		if (new_desc == NULL)
			goto error;

		new_desc->fT = parent->fdt[i]->fT;
		new_desc->share = 1;

		/* 일반 파일일 때만 file_duplicate */
		if (new_desc->fT == FD_FILE) {
			new_desc->file = file_duplicate(parent->fdt[i]->file);

			if (new_desc->file == NULL) {
				free(new_desc);
				goto error;
			}
		}

		current->fdt[i] = new_desc;
	}

		process_init ();

		/* Finally, switch to the newly created process. */

		if_.R.rax = 0;
		info->success = true;
		sema_up(&info->sema);
		do_iret (&if_);

			
	error:
		info->success = false;
		sema_up(&info->sema);
		thread_exit ();
}
int 
find_back(struct file_descriptor **fdt, struct file_descriptor *cur, int fd){
	for(int i = fd-1; i >= 0; i--){
		if(fdt[i] == cur){
			return i;
		}
	}
	return -1;
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

	struct thread *t = thread_current();

	/* We first kill the current context */
	process_cleanup ();

	/* And then load the binary */
	char* save_ptr;
	char* buffer;
	char* letter;
	char** argv;
	int argc = 0;

	buffer = malloc(strlen(file_name)+1);
	if(buffer == NULL){
		palloc_free_page(file_name);
		return -1;
	} 

	strlcpy(buffer, file_name, strlen(file_name)+1);

	bool in_word = false;
	for (int i = 0; file_name[i] != '\0'; i++) {
		if (file_name[i] == ' ') {
			in_word = false;
		}
		else if (!in_word) {
			argc++;
			in_word = true;
		}
	}

	argv = (char **)malloc((argc+1) * sizeof(char*));
	if(argv == NULL){
		palloc_free_page(file_name);
		free(buffer);
		return -1;
	} 

	letter = strtok_r(buffer, " ", &save_ptr);

	for (int i = 0; letter != NULL; i++)
	{
		argv[i] = letter;
		letter = strtok_r(NULL, " ", &save_ptr);
	}
	argv[argc] = NULL;

	success = load (argv[0], &_if);

	if(success) setup_arguments(&_if, argc, argv);
	else{
		palloc_free_page(file_name);
		free(buffer);
		free(argv);
		return -1;
	}
	free(argv);
	free(buffer);

	/* If load failed, quit. */
	palloc_free_page (file_name);
	
	if (!success)
		return -1;

	/* Start switched process. */
	do_iret (&_if);
	NOT_REACHED ();
}

static bool
setup_arguments(struct intr_frame *if_, int argc, char **argv){

	uint64_t rsp = if_->rsp;
	uint64_t zero = 0;
	//사용자 스택에 문자열 저장
	for(int i = argc-1; i >=0; i--){
		size_t len = strlen(argv[i]) + 1;

		stack_push(&rsp, argv[i], len);
		
		argv[i] = (char*)rsp; //첫번째 주소 저장
	}
	//스택 정렬 패딩 삽입
	size_t data_byte = (argc +1)*sizeof(char *);
	size_t padding = (rsp - data_byte)%16;
	rsp -= padding;
	//스택에 0 저장
	stack_push(&rsp, &zero, sizeof(zero));
	for(int i = argc - 1; i >= 0; i--){
		uint64_t address = (uint64_t)argv[i];
		stack_push(&rsp, &address, sizeof(address));
	}
	if_->R.rdi = argc;
	if_->R.rsi = rsp;
	//가짜주소
	stack_push(&rsp, &zero, sizeof(zero));
	if_->rsp = rsp;
	return true;
}

static void
stack_push(uint64_t *rsp, const void *data, size_t size){
	*rsp -= size;
	memcpy((void *)*rsp, data, size);
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
process_wait (tid_t child_tid) {
	struct thread *cur_thread = thread_current ();
	struct thread *child = NULL;
	struct list_elem *e;

	for (e = list_begin (&cur_thread->childs); e != list_end (&cur_thread->childs); e = list_next (e)) {
		struct thread *t = list_entry (e, struct thread, child_elem);
		if (t->tid == child_tid) {
			child = t;
			break;
		}
	}

	if (child == NULL)
		return -1;

	sema_down (&child->parent_sema);        // 자식이 끝날 때까지 대기
	int status = child->exit_status;        // 해제되기 전에 읽기
	list_remove (&child->child_elem);       // 두 번 wait 방지
	sema_up (&child->child_sema);           // 이제 자식이 죽어도 됨

	return status;
}

void
process_exit (void) {
	struct thread *curr = thread_current ();

	if (curr->pml4 != NULL)
		printf ("%s: exit(%d)\n", curr->name, curr->exit_status);
	
	process_cleanup ();
	file_close (curr->running_file);

	if (curr -> fdt != NULL){
		for (int i= 0; i<FILE_MAX; i++){
			if (curr->fdt[i] != NULL){
				if (curr->fdt[i]->share == 1){
					if (curr->fdt[i]->fT == FD_FILE){
						file_close(curr -> fdt[i]->file);
					}
					free(curr->fdt[i]);
				}
				else{ 
					if(curr->fdt[i]->share >= 2){
						curr->fdt[i]->share --;
					}
				}
			}
		}
	}
	free(curr -> fdt);
	while (!list_empty (&curr->childs)) { // 자식 죽기 직전에 부모 기다림, thread_exit까지 간다면 struct thread가 담긴 페이지 해제
		// 부모가 나중에 wait해서 exit_status를 읽으려 할 시 이미 해제된 메모리를 읽음
		struct list_elem *e = list_pop_front (&curr->childs);
		struct thread *c = list_entry (e, struct thread, child_elem);
		sema_up (&c->child_sema); //이 부분이 부모가 더 이상 너를 필요로 하지 않는다고 말하는 부분 
	} // 자식이 바로 죽는 것이 아닌 부모가 exit_status를 읽기 위해 좀비 상태로 버티는 중

	sema_up (&curr->parent_sema);    // 부모 깨우기
	sema_down (&curr->child_sema);   // 부모가 status 읽을 때까지 대기
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
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
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



	/* Allocate and activate page directory. */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());


	/* Open executable file. */
	file = filesys_open (file_name);
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}
	//추가
	file_deny_write(file); //실행 중 읽기 불가
	t->running_file = file; //스레드가 실행중인 파일 저장

	/* Read and verify executable header. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
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
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */



	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	//file_close (file); 이 로직은 로딩하자마자 파일을 닫아버림, 그래서 file_deny_write가 취소됨
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
/* 이 블록의 코드는 프로젝트 2에서만 사용됩니다.
 * 프로젝트 2 전체에 대해 이 함수를 구현하고 싶다면, 구현하세요
 #ifndef 매크로 밖에서. */

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

static struct vm_entry*
vm_entry_init(size_t VM_BIN, uint8_t vaddr, uint32_t zero_bytes,
	uint32_t read_bytes, off_t offset, struct file *v_file
){
	struct vm_entry *v_entry;
	v_entry -> VM_BIN = VM_BIN; // 여기 뭐 넣어야 하는지 모르겠음
	v_entry -> vaddr = vaddr;
	v_entry -> zero_bytes = zero_bytes;
	v_entry -> read_bytes = read_bytes;
	v_entry -> offset = offset;
	v_entry -> file = v_file;
	return v_entry;
}

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
static bool
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
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* 여기부터는 프로젝트 3 이후에 사용할 코드입니다.
 * 프로젝트 2에만 해당 함수를 구현하려면, 어디에 구현하시겠습니까
 * 상층 블록. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
	struct vm_entry *aux1 = (struct vm_entry *)aux;
	uint8_t *kva = page->frame->kva;

	if (file_read_at(aux1->file,kva, aux1->read_bytes, aux1->offset)!=(int32_t)aux1->read_bytes){
		free(aux1);
		return false;
	}
	memset (kva + aux1->read_bytes, 0, aux1 -> zero_bytes);
	free(aux1);
	return true;
}

/* FILE의 OFS 오프셋부터 시작하는 세그먼트를 주소에서 로드합니다
 * 재출시.  총 READ_BYTES + ZERO_BYTES 바이트의 가상
 * 메모리는 다음과 같이 초기화됩니다:
 *
 * - READ_BYTES 바이트는 FILE에서 읽어야 합니다
 * OFS 오프셋에서 시작합니다.
 *
 * - UPAGE와 READ_BYTES에 해당하는 ZERO_BYTES 바이트를 0으로 초기화해야 합니다.
 *
 * 이 함수로 초기화된 페이지는 쓰기가 가능해야 합니다
 * WRITABLE이 true이면 사용자 프로세스, 그렇지 않으면 읽기 전용 프로세스입니다.
 *
 * 메모리 할당 오류가 발생하면 false, 성공하면 true를 반환합니다
 * 또는 디스크 읽기 오류가 발생합니다. */
// 가상 페이지에 담을 파일 포인터, 파일 오프셋, 가상 페이지를 적재할 가상 주소(upage)
// 페이지에서 읽어야할 바이트, 0으로 패딩해줘야 할 바이트

static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* 이 페이지를 어떻게 채울지 계산합니다.
         * FILE에서 PAGE_READ_BYTES 바이트를 읽고
         * 남은 PAGE_ZERO_BYTES 바이트는 0으로 초기화합니다. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE; // 4kb까지만 읽어라
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct vm_entry *v_entry = malloc(sizeof(struct vm_entry));
		// struct supplemental_page_table *spt = &thread_current()->spt;
		// printf("hash : %p\n", &spt->hash_list);
		v_entry -> zero_bytes = page_zero_bytes;
		v_entry -> read_bytes = page_read_bytes;
		v_entry -> offset = ofs;
		v_entry -> file = file;
	
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, v_entry)){
			free(v_entry);
			return false;
		}
		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
		ofs += page_read_bytes;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	if (vm_alloc_page (VM_ANON | VM_MARKER_0, stack_bottom, true)) {
		success = vm_claim_page (stack_bottom);
		if (success) {
			if_->rsp = USER_STACK;
		}
	}
	return success;
}
#endif /* VM */
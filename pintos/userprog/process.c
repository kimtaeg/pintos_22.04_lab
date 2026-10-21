#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "userprog/syscall.h"
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
#include "devices/timer.h"
#include "intrinsic.h"
#ifdef VM
#include "vm/vm.h"
#endif

/* Sizes of process args*/
#define MAX_ARGS 32

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);

/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

static struct process_list process_list;

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	tid_t tid;

	char name_copy[16];
	char *save_ptr;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);
	strlcpy (name_copy, file_name, sizeof(name_copy));

	/* Create a new thread to execute FILE_NAME. */
	tid = thread_create (strtok_r (name_copy, " ", &save_ptr), PRI_DEFAULT, initd, fn_copy);
	if (tid == TID_ERROR)
	{
		palloc_free_page (fn_copy);
		return TID_ERROR; // 종료시켜버림.
	}

	struct process_desc *p = process_desc_create(tid, thread_current()->tid);
	if (p != NULL)
		process_desc_add(p);
	return tid;
}

/* A thread function that launches first user process. */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	process_init ();

	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t
process_fork (const char *name, struct intr_frame *if_ ) {
	struct thread *cur = thread_current();
	// 부모 레지스터 상태 저장 
	memcpy (&cur->parent_if, if_, sizeof(struct intr_frame));
	// 자식 생성
	tid_t child_tid = thread_create(name, PRI_DEFAULT, __do_fork, cur);
	// 생성에 실패
	if(child_tid == TID_ERROR){
		return TID_ERROR;
	}
	// 자식이 복제할 때까지 대기타는거
	sema_down (&cur->fork_sema);

	// 복제 실패와 성공
	if(!cur->fork_success){
		return TID_ERROR;
	}
	return child_tid;

	/* Clone current thread to new thread.*/
	return thread_create (name,
			PRI_DEFAULT, __do_fork, thread_current ());
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
	// va 검사
	if (is_kernel_vaddr(va))
		return true;
	/* 2. Resolve VA from the parent's page map level 4. */
	// 부모의 실제페이지를 parent_page에 담음
	parent_page = pml4_get_page (parent->pml4, va);

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */
	newpage = palloc_get_page(PAL_USER);
	// 메모리 부족하면 
	if (newpage == NULL)
		return false;

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	// 부모 페이지를 자식 페이지로 복사
	memcpy (newpage, parent_page, PGSIZE);
	// 권한 주기
	writable = is_writable(pte);

	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		// 등록 실패 - 받아둔 새 페이지 돌려주기
		palloc_free_page (newpage);
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
	struct thread *parent = (struct thread *) aux;	// 부모	
	struct thread *current = thread_current ();		// 자식
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	// 부모가 저장해둔 레지스터 상태를 가리킴
	struct intr_frame *parent_if = &parent->parent_if;

	bool succ = true;

	/* 1. Read the cpu context to local stack. */
	memcpy (&if_, parent_if, sizeof (struct intr_frame));
	// 0으로 반환값 변경
	if_.R.rax = 0;  

	/* 2. Duplicate PT */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;

	process_activate (current); // 자식 목차로 전환
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent)) // 부모 목차 한 줄씩 → duplicate_pte
		goto error;
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/
	// 부모 fd 테이블의 행 자식 테이블에 복사
	for(struct list_elem *e = list_begin (&parent->fd_table_list);
		e != list_end (&parent->fd_table_list);
		e = list_next (e)) {
		struct fd_row *row = list_entry(e, struct fd_row, elem);
		// 부모 행의 파일 복제
		struct file *dup = file_duplicate(row->file);

		// 실패
		if (dup == NULL)
			goto error;
		// 자식용 행 만들기
		struct fd_row *new_row = fd_row_init(row->value, dup);
	
		// 복제한 파일 정리
		if (new_row == NULL){
			// 복제한 파일 닫기
			file_close (dup);
			goto error;
		}
		// 자식 리스트 추가
		list_push_back (&current->fd_table_list ,&new_row->elem);
		current->next_fd = parent->next_fd;
	}

	process_init ();

	/* Finally, switch to the newly created process. */
	if (succ){
		struct process_desc *p = process_desc_create (current->tid, parent->tid);
		if (p == NULL)
			goto error;
		process_desc_add (p);
		// 부모에서 성공 알리기
		parent->fork_success = true;
		// 부모 깨우기
		sema_up (&parent->fork_sema);
		// 유저 모드로
		do_iret (&if_);
	}
error:
	// 부모에 실패 알리기
	parent->fork_success = false;
	// 부모 깨우기
	sema_up (&parent->fork_sema);
	thread_exit ();
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
int
process_exec (void *f_name) {
	char *file_name = f_name;
	bool success;
	int argc = 0;
	uint64_t argv[MAX_ARGS + 1];
	uint64_t argv_addr[MAX_ARGS];
	char *tokens[MAX_ARGS];
	char *token, *save_ptr;

	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context */
	process_cleanup ();

	for (token = strtok_r (file_name, " ", &save_ptr); token != NULL; token = strtok_r (NULL, " ", &save_ptr)) {

		if (argc >= MAX_ARGS) {
			palloc_free_page (file_name);
			return -1;
		}

		tokens[argc] = token;
		argc += 1;
	}

	if (argc == 0) {
		palloc_free_page (file_name);
		return -1;
	}

	/* And then load the binary */
	lock_acquire(&filesys_lock);
	success = load (tokens[0], &_if);
	lock_release(&filesys_lock);

	if (success) {
	
		for (int i = argc - 1; i >= 0; i -= 1) {
			_if.rsp -= strlen(tokens[i]) + 1;
			memcpy((void *) _if.rsp, tokens[i], strlen(tokens[i]) + 1);
			argv_addr[i] = _if.rsp;
		}

		_if.rsp -= _if.rsp % 8;

		_if.rsp -= 8;
		argv[argc] = _if.rsp;

		
		for (int i = argc - 1; i >= 0; i -= 1) {
			_if.rsp -= 8;
			*(uint64_t *) _if.rsp = argv_addr[i];
			argv[i] = _if.rsp;
		}

		_if.rsp -= 8;		

		_if.R.rdi = argc;
		_if.R.rsi = argv[0];
	}

	/* If load failed, quit. */
	palloc_free_page (file_name);
	if (!success)
		return -1;

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
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */

	return process_desc_wait (child_tid);
}

/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
	struct thread *curr = thread_current ();
	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */

	printf ("%s: exit(%d)\n", curr->name, curr->exit_status);
	process_desc_exit (curr->exit_status);
	fd_table_cleanup ();
	process_cleanup ();
	process_kill_children(curr->tid);
}

/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

	if(curr->running_file != NULL){
		lock_acquire(&filesys_lock);
		file_close(curr->running_file);
		lock_release(&filesys_lock);
		curr->running_file = NULL;
	}

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

/* ProcessListController */

/**
 * @brief process_desc 전역 목록과 lock을 초기화한다. 부팅 시 1번만 호출.
 */
void process_list_init(void)
{
	list_init(&process_list.list);
	lock_init(&process_list.lock);
}

/**
 * @brief process_desc 레코드를 생성한다. exit_status는 -1, semaphore는 0으로 시작.
 * @param tid  새 프로세스의 tid
 * @param ppid 부모 프로세스의 tid
 * @return 생성한 레코드. 메모리 부족 시 NULL
 */
struct process_desc *process_desc_create(tid_t tid, tid_t ppid)
{
	struct process_desc *p = malloc(sizeof(struct process_desc));
	if (p == NULL)
		return NULL;

	p->tid = tid;
	p->ppid = ppid;
	p->exit_status = -1;

	sema_init(&p->semaphore, 0);

	return p;
}

/**
 * @brief 레코드를 목록 끝에 추가한다. (lock 보호)
 * @param p 추가할 레코드 (process_desc_create로 만든 것)
 */
void process_desc_add(struct process_desc *p)
{
	lock_acquire(&process_list.lock);
	list_push_back(&process_list.list, &p->elem);
	lock_release(&process_list.lock);
}

/**
 * @brief 레코드를 목록에서 빼고 free한다. (lock 보호)
 * @param p 제거할 레코드. 호출 후 사용 금지
 */
void process_desc_remove(struct process_desc *p)
{
	lock_acquire(&process_list.lock);
	list_remove(&p->elem);
	lock_release(&process_list.lock);
	free(p);
}

/**
 * @brief tid로 레코드를 검색한다. lock은 검색 중에만 잡고 반환 전에 푼다.
 * @param tid 찾을 프로세스의 tid
 * @return 찾은 레코드. 없으면 NULL
 */
struct process_desc *process_desc_find(tid_t tid)
{
	lock_acquire(&process_list.lock);
	for (struct list_elem *e = list_begin(&process_list.list); e != list_end(&process_list.list); e = list_next(e))
	{
		struct process_desc *p = list_entry(e, struct process_desc, elem);
		if (tid == p->tid)
		{
			lock_release(&process_list.lock);
			return p;
		}
	}
	lock_release(&process_list.lock);
	return NULL;
}

/**
 * @brief ppid가 tid인 레코드(= 해당 프로세스의 자식)를 모두 제거하고 free한다.
 *        부모가 종료할 때 호출. 순회 중 삭제하므로 next_e를 미리 저장해 둔다.
 * @param tid 부모 프로세스의 tid
 */
void process_kill_children(tid_t tid)
{
	lock_acquire(&process_list.lock);
	struct list_elem *e = list_begin(&process_list.list);
	while (e != list_end(&process_list.list))
	{
		struct list_elem *next_e = list_next(e);
		struct process_desc *p = list_entry(e, struct process_desc, elem);
		if (tid == p->ppid)
		{
			list_remove(&p->elem);
			free(p);
		}

		e = next_e;
	}
	lock_release(&process_list.lock);
}

// int process_desc_sema(tid_t tid, enum sema_op op, int status)
// {
// 	struct process_desc *p = process_desc_find(tid);
// 	if (p == NULL || tid != p->ppid)
// 		return -1;
// 	switch (op)
// 	{
// 	case SEMA_DOWN:
// 		if (p->ppid != thread_current()->tid)
// 			return -1;
// 		sema_down(&p->semaphore);
// 		return p->exit_status;
// 	case SEMA_UP:
// 		p->exit_status = status;
// 		sema_up(&p->semaphore);
// 		break;
// 	}
// 	return 0;
// }

/**
 * @brief [자식 쪽] 자기 tid의 레코드에 종료 상태를 기록하고 sema_up으로 부모를 깨운다.
 *        레코드가 없으면(부모가 먼저 죽어 정리됨) 아무것도 하지 않는다.
 * @param status 종료 상태 (exit 인자, 예외로 죽으면 -1)
 */
void process_desc_exit (int status){
	struct process_desc *p = process_desc_find(thread_current()->tid);
	if (p == NULL)
		return;
	p->exit_status = status;
	sema_up(&p->semaphore);
}

/**
 * @brief [부모 쪽] 자식이 끝날 때까지 sema_down으로 기다린 뒤 종료 상태를 반환하고
 *        레코드를 삭제한다.
 * @param child_tid 기다릴 자식의 tid
 * @return 자식의 종료 상태.
 *         레코드가 없거나(없는 pid, 이미 wait함) 내 자식이 아니면 -1
 */
int process_desc_wait (tid_t child_tid){
	struct process_desc *p = process_desc_find(child_tid);
	if (p == NULL || p->ppid != thread_current()->tid)
		return -1;

	sema_down(&p->semaphore);
	int status = p->exit_status;
	process_desc_remove(p);
	return status;
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

static bool setup_stack (struct intr_frame *if_);
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

	file_deny_write(file);

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

	if(success){
		thread_current()->running_file = file; 
	}else{
		file_close (file);
	}
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
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
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
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		void *aux = NULL;
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, aux))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */

	return success;
}
#endif /* VM */

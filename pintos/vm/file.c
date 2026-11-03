/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#define PAGE_SIZE 4096
#include "userprog/process.h"
#include "threads/mmu.h"
#include "threads/malloc.h"
#include "userprog/syscall.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
	
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	struct vm_entry *aux = page->uninit.aux;
	/* Set up the handler */
	page->operations = &file_ops;
	struct file_page *file_page = &page->file;
	file_page->file = aux->file;
	file_page->offset = aux->offset;
	file_page->read_bytes = aux->read_bytes;
	file_page->zero_bytes = aux->zero_bytes;
	return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page = &page->file;
	// 지금 프레임이 끊긴 상황 (out 선행)
	// 프레임 연결만 하면 됨
	// file_read_at()으로 offset 위치로 가서 read_bytes + zero_bytes 
	off_t ofs = file_page -> offset;
	size_t read_bytes = file_page -> read_bytes;
	size_t zero_bytes = file_page -> zero_bytes;
	struct file * file = file_page->file;

	bool held = lock_held_by_current_thread (&file_lock);
	if (!held) lock_acquire (&file_lock);
	int n = file_read_at (file, kva, read_bytes, ofs);
	if (!held) lock_release (&file_lock);

	if (n!=(int32_t)read_bytes){
		return false;
	}
	memset(kva+read_bytes, 0, zero_bytes);
	return true;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page = &page->file;
	// dirty bit check 하고 만약 사용했다면 -> 파일에 다시 업데이트
	// pml4_claer_page로 매핑을 끊고, dirty bit 초기화
	// swap out 시에 프레임 쪽만 끊어버림
	if (pml4_is_dirty(page->owner->pml4, page->va)){
		bool held = lock_held_by_current_thread (&file_lock);
		if (!held) lock_acquire (&file_lock);
		file_write_at(file_page -> file, page->frame->kva, file_page->read_bytes, file_page->offset);
		if (!held) lock_release (&file_lock);
		pml4_set_dirty(page->owner->pml4, page->va, false);
	}
	page->frame->page = NULL;
	page->frame = NULL;

	pml4_clear_page(page->owner->pml4, page->va);
	return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page = &page->file;
	struct thread *t = thread_current();

	// 프레임이 있는지 확인
	if(page->frame == NULL){
		return;
	}
	// dirty 조건확인
	if(pml4_is_dirty(t->pml4, page->va)){
		bool held = lock_held_by_current_thread (&file_lock);
		if (!held) lock_acquire (&file_lock);
		file_write_at(file_page -> file, page->frame->kva, file_page->read_bytes, file_page->offset);
		if (!held) lock_release (&file_lock);
		// dirty 비트 내리기
		pml4_set_dirty(t->pml4, page->va, false);
	} 
	// pml4 매핑 제거
	pml4_clear_page(t->pml4, page->va);
	// 프레임테이블에서 제거 -> 물리 메모리 해제 -> 구조체 해제 -> null 순서 중요
	// vm_frame_remove(page->frame);
	vm_frame_remove(page->frame);
	page->frame = NULL;
	// 파일은 닫지 않기 -> 매핑의 페이지들이 파일을 공유해서 do munmap에서 한번만 닫으면 됨
}
static bool 
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
	struct vm_entry *aux1 = (struct vm_entry *)aux;
	uint8_t *kva = page->frame->kva;


	bool held = lock_held_by_current_thread (&file_lock);
	if (!held) lock_acquire (&file_lock);
	int n = file_read_at(aux1->file,kva, aux1->read_bytes, aux1->offset);
	if (!held) lock_release (&file_lock);

	if (n!=(int32_t)aux1->read_bytes){
		free(aux1);
		return false;
	}
	memset (kva + aux1->read_bytes, 0, aux1 -> zero_bytes);
	free(aux1);
	return true;
}
/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
		int64_t addr_map = addr;
		if (offset % PGSIZE != 0) return NULL;                                    /* mmap-bad-off */
		if ((uint64_t) addr + length < (uint64_t) addr) return NULL;              /* 주소 오버플로 */
		if (is_kernel_vaddr (addr) || is_kernel_vaddr (addr + length - 1)) return NULL;  /* mmap-kernel */
			
		if (addr_map % PAGE_SIZE == 0 && addr_map!=0){ //정렬시켰는지 확인
			if(length <= 0 ) return NULL;
			int pagenum = (length + PAGE_SIZE - 1) / PAGE_SIZE; //나눈다음 페이지 개수 4KB 크기
			struct thread *thread = thread_current();
			int64_t in = length;
			bool held = lock_held_by_current_thread (&file_lock);
			if (!held) lock_acquire (&file_lock);
			int64_t size = file_length(file) - offset;
			struct file *reopened = file_reopen(file);
			if (!held) lock_release (&file_lock);
			// if(in < size)return NULL; //매핑할 크기가 파일크기보다 작다면 
			if (size > in) size = in;
			// struct file *file_struct = file_reopen(file);
			struct file_page file_page;//구지 말록으로 할 필요 없음
			file_page.file = file_reopen(file); //한번만 open 시키기 위해 반복문 밖에 설치
			file_page.zero_bytes = length - size; //매핑 길이 - 파일 크기 해서 zero 넣기

			for (int i=0;i<pagenum;i++){ //페이지 개수만큼 보내기
				//왜 vm_entry 냐면 aux를 가져오기 위해서 loazy_load_sement 를 사용하는데 이게 vm_entry로 매서로 가짐 그래서 vmn_entry로 하는거
				struct vm_entry *vm_entry = malloc(sizeof(struct vm_entry)); //말록으로 힙에 할당하기
				vm_entry->file = file_page.file;
				vm_entry->offset = offset; //돌때마다 하기
				
				if (size>=PAGE_SIZE){ //만약 길이가 4KB 와 같거나 크다면
					vm_entry->read_bytes = PAGE_SIZE;
					vm_entry->zero_bytes = 0;
					size -= PAGE_SIZE;
				}

				else if(size<PAGE_SIZE && size > 0){ //만약 길이가 4KB 보다 작다면 즉 마지막일때
					vm_entry->read_bytes = size;
					size -= PAGE_SIZE;
					vm_entry->zero_bytes = abs(size);
				}
				else{//만약 사이즈가 다 떨어졌다면
					vm_entry->read_bytes =0;
					vm_entry->zero_bytes =PAGE_SIZE;
				}
				vm_entry->offset += PAGE_SIZE*i;//처음에 0 두번째에 4000 세번쩨에 8000 마지막은 끝


				if(!vm_alloc_page_with_initializer(VM_FILE,addr + i*PAGE_SIZE,writable,lazy_load_segment,vm_entry)) {	
					for(int j=0 ; j<i ; j++){//일단 반복은 시켰는데 이게 페이지사이즈 만큼 내리면서 하면 될꺼 같은데 일단 여기까지
						free(vm_entry);
					}
					 //여러번 시켜야 하는데 일단 설계만
					//만약 실패하면 현재 i번으로 페이지 만큼 빼면서 하면 될꺼 같은데
					return NULL; // VM에 페이지 만들기 실패하면 NULL 반환
				}
				
			}
			struct page *page = spt_find_page(&thread->spt,addr); //spt 안에 페이지 가져오기
			page->num = pagenum; //page 안에 페이지 총 개수 넣기
			return addr;
		}
		else{
			return NULL;
		}
}

/* Do the munmap */
void
do_munmap (void *addr) {
	struct thread *cur_thread = thread_current();
	struct page *cur_page = spt_find_page(&cur_thread->spt, addr);
	if (cur_page == NULL) return ;
	struct file_page *cur_file_page = &cur_page->file;
	
	struct file *file;
	int num = cur_page->num;
	if (num == 0) return ; // 만약 num이 0이면 mmap 관련 페이지가 아니므로 날림

	if (VM_TYPE (cur_page->operations->type) == VM_UNINIT) { // uninit과 file 인 경우 file에 접근하는 구조체가 다름
		struct vm_entry *aux = cur_page->uninit.aux;
		file = aux->file;
	} else {
		file = cur_page->file.file;
	}
	
	int count = 0;

	while (count != num){
		struct page *cur_page = spt_find_page(&cur_thread->spt, addr); // 현재 페이지를 가져옴
		if (cur_page == NULL) return ; // 만약 페이지가 없으면 오류
		struct file_page *cur_file_page = &cur_page->file; 
		
		hash_delete(&cur_thread->spt.hash_list, &cur_page->hash_elem); // hash_list에서 제거
		vm_dealloc_page(cur_page); // destroy 등의 작업
		
		addr += PGSIZE; //다음 페이지
		count ++;  // count를 증가시킴
	}
	bool held = lock_held_by_current_thread (&file_lock);
	if (!held) lock_acquire (&file_lock);
	file_close(file);
	if (!held) lock_release (&file_lock); // 반복문을 통하여 전체 페이지를 전부 없앴기 때문에 file을 닫음
}

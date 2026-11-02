/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/mmu.h"

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
	/* Set up the handler */
	page->operations = &file_ops;
	struct file_page *file_page = &page->file;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
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
		file_write_at(file_page->file, page->frame->kva, file_page->read_bytes, file_page->offset);
		// dirty 비트 내리기
		pml4_set_dirty(t->pml4, page->va, false);
	} 
	// pml4 매핑 제거
	pml4_clear_page(t->pml4, page->va);
	// 프레임테이블에서 제거 -> 물리 메모리 해제 -> 구조체 해제 -> null 순서 중요
	vm_frame_remove(page->frame);
	palloc_free_page(page->frame->kva);
	free(page->frame);
	page->frame = NULL;
	// 파일은 닫지 않기 -> 매핑의 페이지들이 파일을 공유해서 do munmap에서 한번만 닫으면 됨
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
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
	file_close(file); // 반복문을 통하여 전체 페이지를 전부 없앴기 때문에 file을 닫음
}
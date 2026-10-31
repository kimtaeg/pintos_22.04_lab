/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"

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
}

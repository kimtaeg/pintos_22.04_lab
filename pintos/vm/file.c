/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#define PAGE_SIZE 4096
#include "userprog/process.h"
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
	struct file_page *file_page UNUSED = &page->file;
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
/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
		struct thread *thread = thread_current();
		
		int64_t addr_map = addr;
		if(length <= 0 ) return NULL;
		if (addr_map % PAGE_SIZE == 0 && addr_map!=0){ //정렬시켰는지 확인
			int pagenum = (length + PAGE_SIZE - 1) / PAGE_SIZE; //나눈다음 페이지 개수 4KB 크기

			int64_t off = offset;
			int64_t in = length;
			int64_t size = file_length(file);
			// struct file *file_struct = file_reopen(file);
			struct file_page file_page;//구지 말록으로 할 필요 없음
			file_page.file = file_reopen(file); //한번만 open 시키기 위해 반복문 밖에 설치


			for (int i=0;i<pagenum;i++){ //페이지 개수만큼 보내기
				//왜 vm_entry 냐면 aux를 가져오기 위해서 loazy_load_sement 를 사용하는데 이게 vm_entry로 매서로 가짐 그래서 vmn_entry로 하는거
				struct vm_entry *vm_entry = malloc(sizeof(struct vm_entry)); //말록으로 힙에 할당하기
				vm_entry->file = file_page.file;
				vm_entry->offset = off; //돌때마다 하기
				
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
					free(vm_entry); //여러번 시켜야 하는데 일단 설계만
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
	/*이놈은 RAM 안에 있는 프레임과 VM에 페이지들을 싹다 free 시키거나 SPT를 제거하거나 하는등의 일을 함*/
}

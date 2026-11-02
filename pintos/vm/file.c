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
		/*하는일 : 
		일단 원래 는 디스크에서 물리메모리 에 커널버퍼라는 곳에 올리고 유저버퍼에 디스크 파일을 다시 옮겨야했음
		즉 다시말해서 RAM 안에서 2번의 움직임이 필요하다. 유저 버퍼에 파일이 VM 버퍼로 가는거지 
		순서가 매핑된척 -> 사용 -> 폴트 -> 프레임할당 + 디스크 올림 -> 다시*/
		//이게 맞는지 검사 정렬된 상태인지 길이 등등
		int64_t addr_map = addr;
		if(length <= 0 ) return NULL;
		if (addr_map % PAGE_SIZE == 0 && addr_map!=0){ //정렬시켰는지 확인
			int pagenum = (length + PAGE_SIZE - 1) / PAGE_SIZE; //나눈다음 페이지 개수 4KB 크기
			
			int64_t off = offset;
			bool bool_mmap ;
			int64_t in = length;
			void *aux;
			for (int i=0;i<pagenum;i++){ //페이지 개수만큼 보내기
			struct vm_entry *file_page = malloc(sizeof(struct vm_entry)); //말록으로 힙에 할당하기
			file_page->offset = off; //VM_entry 에 오프셋을 off로 바꾸기
			if(in>=PAGE_SIZE)file_page->zero_bytes = 0;
			file_page->file = file_reopen(file);

				if (in>=PAGE_SIZE){ //만약 길이가 4KB 와 같거나 크다면
					file_page->read_bytes = PAGE_SIZE;
					
					in -= PAGE_SIZE;
				}
				else if(in<PAGE_SIZE){ //만약 길이가 4KB 보다 작다면 즉 마지막일때
					file_page->read_bytes = in;
					in -= PAGE_SIZE;
					file_page->zero_bytes = abs(in);
				}
				
				vm_alloc_page_with_initializer(VM_FILE,addr + i*PAGE_SIZE,writable,lazy_load_segment,file_page); // VM에 페이지 만들기
				off += PAGE_SIZE; //다음부터는 계속 커져야 하니까 += 로 넣기
				file_page->offset = off;
				// vm_alloc_page(VM_FILE,addr + i*PAGE_SIZE,writable);
				// in -= PAGE_SIZE;//페에지 크기
			}
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

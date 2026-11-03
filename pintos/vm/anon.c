/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "lib/kernel/bitmap.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static struct lock swap_lock; // 스왑 디스크 접근 동기화용 락
static struct bitmap *swap_table; // 스왑 디스크 사용 여부를 나타내는 비트맵
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

#define SWAP_SECTOR_SIZE 512 // 스왑 디스크의 섹터 크기
#define SWAP_SECTORS_PER_PAGE (PGSIZE / SWAP_SECTOR_SIZE) // 한 페이지를 구성하는 섹터 수

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* 
	TODO: Set up the swap_disk. 
	익명 페이지 하위 시스템을 초기화합니다. 
	이 함수에서는 익명 페이지와 관련된 모든 설정을 할 수 있습니다.
	*/
	// 스왑 디스크를 가져옴
	swap_disk = disk_get(1, 1); // 컨트롤러, 디스크
	if(swap_disk == NULL){
		PANIC("Swap disk not found");
	}
	// 디스크 전체 섹터 수 -> 슬롯 수로 변환
	size_t slot_cnt = disk_size(swap_disk) / SWAP_SECTORS_PER_PAGE;
	// 슬롯 수 만큼 비트 생성
	swap_table = bitmap_create(slot_cnt);
	if(swap_table == NULL){
		PANIC("swap table allocation failed");
	}
	// 여러 프로세스가 동시에 스왑할 수 있으니
	lock_init(&swap_lock);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	// struct uninit_page *uninit_page = &page->uninit;
	// void *aux = uninit_page->aux; 
	page->operations = &anon_ops;
	struct anon_page *anon_page = &page->anon;
	// anon_page->aux = aux;
	// anon_page->type = type;
	anon_page->slot = BITMAP_ERROR;
	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
	// 몇번 슬롯인지 저장
	size_t slot = anon_page->slot;
	if(slot == BITMAP_ERROR){
		return false;
	}
	// 8섹터를 읽어서 kva에 채움
	for(int i=0; i<SWAP_SECTORS_PER_PAGE;i++){
		disk_sector_t ser_no = slot * SWAP_SECTORS_PER_PAGE + i;
		void *buf = (uint8_t *) kva + i * DISK_SECTOR_SIZE;
		disk_read(swap_disk, ser_no, buf);
	}
	// 슬롯 반납
	lock_acquire(&swap_lock);
	bitmap_reset(swap_table, slot);
	lock_release(&swap_lock);
	// 스왑 디스크 없다 표시
	anon_page->slot = BITMAP_ERROR;

	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	// 빈 슬롯 찾고 락걸기
	lock_acquire(&swap_lock);
	size_t slot = bitmap_scan_and_flip(swap_table, 0, 1, false);
	lock_release(&swap_lock);
	if(slot == BITMAP_ERROR){
		PANIC("swap disk is full");
	}

	// 프레임 내용 8섹터에 쓰기
	void *kva = page->frame->kva;
	for(int i = 0; i < SWAP_SECTORS_PER_PAGE; i++){
		disk_sector_t ser_no = slot * SWAP_SECTORS_PER_PAGE + i;
		void *buf = (uint8_t *) kva + i * DISK_SECTOR_SIZE;
		disk_write(swap_disk, ser_no, buf);
	}
	// 슬롯 번호 저장
	anon_page->slot = slot;
	// 매핑 끊기
	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	// swap disk가 있을때 슬롯만 반납
	if(anon_page->slot != BITMAP_ERROR){
		lock_acquire(&swap_lock);
		bitmap_reset(swap_table, anon_page->slot);
		lock_release(&swap_lock);
		anon_page->slot = BITMAP_ERROR;
	}
	// 메모리에 있음 - 프레임 정리
	struct frame *frame = page->frame;
	if(frame != NULL){
		//매핑 끊고 물리 프레임 해제
		pml4_clear_page(page->owner->pml4, page->va);
		//palloc_free_page(frame->kva);
		//free(frame);
		// 프레임 테이블에서 빼기
		vm_frame_remove(frame);
		page->frame = NULL;
	}
}

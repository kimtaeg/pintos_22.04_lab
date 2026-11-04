/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "lib/kernel/hash.h"
#include "filesys/file.h"
#include "vm/uninit.h"
#include "filesys/page_cache.h"
#include "lib/kernel/list.h"
#include "threads/mmu.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
struct list frame_table;
static struct lock frame_table_lock;

void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	list_init(&frame_table);
	lock_init(&frame_table_lock);
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

static unsigned page_hash (const struct hash_elem *e, void *aux);
static bool page_less (const struct hash_elem *a, const struct hash_elem *b, void *aux);
static void page_destructor  (struct hash_elem *e, void *aux);

/* 초기화 함수와 함께 대기 중인 페이지 객체를 생성합니다. 페이지를 직접 생성하지 말고,
 * 반드시 이 함수나 vm_alloc_page를 통해 생성하세요. -> 대기 중인 페이지 객체는 uninit page를 말함 */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)
	struct page* new_page = malloc(sizeof(struct page));
	if (new_page == NULL) return false;
	struct supplemental_page_table *spt = &thread_current ()->spt;
	// 페이지 생성을 위쪽에서 하고, 
	/*
	page_initializer
	-> 해당 페이지를 다른 페이지로 바꿔주는 애

	vm_initializer 
	-> 페이지를 할당하고 그에 대한 4kb 물리적 프레임 이쪽에다가 데이터를 채우는 애
	*/
	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* 할 일: 페이지 생성, VM 타입에 따라 초기화값 가져오기,
		 * TODO: 그 다음 uninit_new를 호출하여 "uninit" 페이지 구조체를 생성합니다. 너
		 * TODO: uninit_new 호출 후 필드를 수정해야 합니다. 
		 * uninit_new를 사용하면 미초기화 페이지를 만들 수 있음 
		 * 미초기화 페이지에 매핑되어야 할 물리 프레임의 정보를 저장하고 이를 spt에 등록한다고 추상화*/
		/* 할 일: 페이지를 spt에 삽입한다. */
		if ((VM_TYPE(type)) == VM_ANON){ // 이 부분 임시코드라 수정 필요 -> 모든 상황마다 uninit_new를 해야하는 지는 모르겠음
			uninit_new(new_page, upage, init, VM_ANON , aux, anon_initializer); 
		}
		else if (VM_TYPE(type) == VM_FILE){
			uninit_new(new_page, upage, init, VM_FILE, aux, file_backed_initializer);
		}
		else{
			free(new_page);
			goto err;
		}
		new_page->rw = writable;
		new_page->owner = thread_current();
		if(spt_insert_page(spt, new_page)==false) {
			free (new_page);
			goto err;
		}
		
		return true;
	}
err:
	return false;
}
// load seg -> aux 정보 자체를 구조체로 넣어줘야 함
// 이 정보가 load_segment의 지역변수로 선언 시에 호출이 끝나면 날라가기 때문에 malloc을 통하여 전역 힙 공간에 선언해주고 free를 어디서 할 지 염두해둬야 함

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	// struct page* page1 = (struct page*)malloc(sizeof(struct page));
	struct page cur_page = {0};
	cur_page.va = pg_round_down(va);
	struct hash_elem* h_e;
	h_e = hash_find(&spt->hash_list, &cur_page.hash_elem);
	if(h_e != NULL){
		return hash_entry(h_e, struct page, hash_elem);
	}
	return NULL;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED, //인설트 담당
		struct page *page UNUSED) {
	int succ = false; //succ 란 성공 했는지 아닌지를 하는 것
	/* TODO: Fill this function. */
	//일단 spt 안에 있는 리스트 안에 page를 넣어! 
	//리턴값으로 주소를 리턴해!
	// hash(va) % bucket_cnt
	//key 가 주소 고 vlaue가 페이지인가? 그래서 주소를 주면 페이지를 가져오는거지 get 하면 아따 좋구먼! 
	//맞네 주소는 16진수 니까 그냥 %8 하면 되구나! 
	//인덱스 0부터 7까지 하고 key를 주소로 하면 될꺼 같은데? 
	//주소는 VM 주소로 해야할듯? SPT 가 물리주소로 하는게 아닌거 같으니까? 

	struct hash_elem h_elem = page -> hash_elem;
	
	if (hash_insert(&spt->hash_list, &page->hash_elem)==NULL){//만약 없어서 새로 넣었다면 NULL 반환 
		//이미 있다면 그냥 그요소를 반환
		succ = true;
	}

	return succ;
}

void
vm_frame_remove (struct frame *frame) {
	if (frame == NULL)
		return;

	lock_acquire (&frame_table_lock);
	list_remove (&frame->frame_elem);
	lock_release (&frame_table_lock);

	palloc_free_page (frame->kva);
	free (frame);
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	hash_delete(&spt->hash_list, &page->hash_elem);
	vm_dealloc_page (page);
	return true;
}


/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) { //내 생각에는 이게 희생시킬 프레임을 고른다.
	struct frame *victim = NULL;
	struct list_elem *victim_elem ;
	/* TODO: The policy for eviction is up to you. */
	// 다른 스레드가 insert/remove 중일 수 있으니 lock 걸고 꺼냄
	lock_acquire(&frame_table_lock);
	if (list_empty(&frame_table)){ //프레임 테이블이 비어 있으면 NULL 반환
		lock_release(&frame_table_lock);
		return NULL;
	}
	victim_elem = list_pop_front(&frame_table);//리스트 가장 첫번째 제거하기 (FIFO)
	lock_release(&frame_table_lock);
	victim = list_entry(victim_elem, struct frame , frame_elem);
	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.
 * 페이지 교체 알고리즘
*/
static struct frame *
vm_evict_frame (void) { //고른 프레임을 실제로 쫓아내기
	struct frame *victim UNUSED = vm_get_victim ();//이게 victim 이 희생자 프레임
	if(victim == NULL) return NULL;
	// file swap_out이 victim->page를 NULL로 만들기 때문에 미리 저장
	struct page *page = victim->page;
	if(page == NULL)return NULL;
	if(!swap_out(page)){ //프레임의 페이지 쫓아내기
		// 실패하면 테이블에 다시 넣어 두기 (빠진 채로 두면 destroy 때 이중 list_remove)
		frame_table_insert(&victim->frame_elem);
		return NULL;
	}
	// victim 페이지는 다른 프로세스 것일 수 있으니 owner의 pml4에서 삭제
	pml4_clear_page(page->owner->pml4, page->va); //PML4 에서도 삭제
	page->frame = NULL ;
	victim->page = NULL;
	//서로 연결되어있는걸 끊어버리기	
	return victim;
	/* TODO: swap out the victim and return the evicted frame. */
	
	
}

/* palloc() 및 프레임 가져오기. 사용 가능한 페이지가 없으면 페이지를 제거합니다
 * 그리고 반환합니다. 항상 유효한 주소가 반환됩니다. 즉, 사용자 풀이
 * 메모리가 가득 찼습니다. 이 함수는 사용 가능한 메모리를 확보하기 위해 프레임을 제거합니다
 *공간.
 `page.page() `를 호출하여 사용자 풀에서 새 물리적 페이지를 가져옵니다 palloc_get_page. 
 사용자 풀에서 페이지를 성공적으로 가져오면 프레임을 할당하고 멤버를 초기화한 후 반환합니다. 
 `page.page()`를 구현한 후에는 vm_get_frame모든 사용자 공간 페이지(PALLOC_USER)를 
 이 함수를 통해 할당해야 합니다. 페이지 할당 실패 시 스왑 아웃 처리는 현재로서는 필요하지 않으며, 
 해당 경우를 `swapout`으로 표시하기만 하면 됩니다 PANIC ("todo") .
 */

static struct frame *
vm_get_frame (void) {
	/* TODO: Fill this function. */
	/*
	요구 사항 다시 정리
	- vm_get_frame 함수를 통하여 사용자 공간 PALLOC_USER 에서 사용될 메모리 페이지를 가져와야 함
	- palloc_get_page() 함수를 호출하여 사용자 풀에서 새 물리적 페이지(프레임)를 가져옴
	- 성공적으로 가져오면 프레임을 할당하고 멤버를 초기화한 다. 음. 바. 화
	- 페이지 할당 실패 시 해당 케이스를 PANIC("todo")로 표시 
	*/
	// void *kva = palloc_get_page(PAL_USER); // 사용자 풀에서 페이지 할당
	// if (kva == NULL){
	// 	PANIC("todo");
	// }
	// frame = malloc(sizeof(struct frame));
	// frame->kva = kva;
	/* 새로운 frame 구조체르 메모리에 할당
	이는 물리 페이지 데이터를  저장할 공간*/
	struct frame *frame = malloc(sizeof(struct frame));
	// 예외 처리 
	ASSERT (frame != NULL);

	/* 실제 물리 페이지 할당 
		PAL_USER : 사용자 프로세스용 메모리 풀에서 할당
		PAL_ZERO : 할당된 페이지 0으로 초기화
		palloc_get_page -> 4kb 페이지 바로 할당해주는 함수	
	*/
	frame->kva = palloc_get_page(PAL_USER | PAL_ZERO);
	
	/* 메모리 부족 상황 처리
	사용 가능한 물리 페이지가 없는 경우*/
	if(frame->kva == NULL){
		//PANIC("todo");
		/* 희생 프레임 선택 및 교체
		vm_evict_frame을 사용함 페이지 교체 알고리즘,
		교체할 프레임을 선택, 이 프레임을 디스크로*/
		struct frame *vicim1 = vm_evict_frame();
		// 예외처리
		if(vicim1 == NULL){
			PANIC("out of memory");
		}

		/* 희생자의 물리 페이지를 재활용
			교체된 프레임의 물리 주소를 새로운 프레임이 사용*/
		frame->kva = vicim1->kva;
		// 이전 데이터가 남아 있으니 PAL_ZERO처럼 0으로 초기화
		memset(frame->kva, 0, PGSIZE);

		/* 희생자 프레임 구조체 해제 */
		free(vicim1);
	}
	/* 새로운 프레임 초기화 아직 가상페이지와 연결 x */
	frame->page = NULL;

	/* 프레임 테이블 등록은 vm_do_claim_page에서 claim이 끝난 뒤에 함
	   (초기화 안 된 uninit 페이지가 victim으로 뽑히지 않게) */

	ASSERT (frame->page == NULL);

	// 할당 프레임 반환
	return frame;
}


/* Growing the stack. */
/* addr이 더 이상 fault 주소가 아니도록 하나 이상의 
	anonymous page를 할당해서 스택 크기를 늘립니다. 할당을 처리할 때는 
	addr를 PGSIZE 단위로 내림 정렬해야 합니다*/
static void
vm_stack_growth (void *addr UNUSED) {
	// addr까지 페이지 경게로 내림
	void *stack_page = pg_round_down(addr);

	/* VM_ANON: 파일과 연결 없는 익명 페이지 
	 * VM_MARKER_0: 이 페이지가 "스택 페이지"라는 표시 
	 * vm alloc page는 spt에 페이지 정보만 등록하고 실제 프레임은 아직 안 붙임 */
	if(vm_alloc_page(VM_ANON | VM_MARKER_0, stack_page, true)){
		// 등록한 페이지에 실제 메모리를 할당하고 페이지 테이블에 매핑
		vm_claim_page(stack_page);
	}
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f, void *addr,
		bool user, bool write, bool not_present) {
	struct supplemental_page_table *spt = &thread_current ()->spt;
	struct page *page = spt_find_page(spt, addr);
	
	// is_kernel_vddd는 KERN_BASE보다 주소값이 클 때, 주소검사
	if (addr == NULL || is_kernel_vaddr(addr))
		return false;

	// not_present는 이 폴트가 페이지가 아예 없어서 난것인가
	// 이미 올라와 있는 페이지에서 폴트 권한 위반	
	if (!not_present)
		return false;
	
	// spt에서 찾기 	
	if (page == NULL){
		/* 기준이 될 유저 스택 포인터를 고른다
		 * true: 유저 코드 실행 중 폴트 → f->rsp가 곧 유저 rsp
		 * false: 시스템 콜 처리 중(커널 모드) 폴트 → f->rsp는 커널 스택이라 못 씀 */
		void *rsp = user ? (void *) f->rsp : thread_current ()->user_rsp;

		// 스택 확장 인정 조건
		if(addr<USER_STACK && addr >= rsp - 8 && addr >= (USER_STACK - (1<<20))){
			// 스택 페이지 추가
			vm_stack_growth(addr);
			return true;
		}
		return false;
	}	

	// 새 페이지때 쓰기 권한, 읽기 전용인데 쓰면 안됨
	if (write && page->rw == false)
		return false;

	return vm_do_claim_page (page);
	// vm_do_claim_page 함수
	// : 페이지의 VA를 프레임의 PA에 매핑하는 페이지 테이블 항목을 삽입하세요.
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
/* va에 해당하는 page를 claim합니다. 먼저 page를 얻은 뒤,
   그 page로 vm_do_claim_page를 호출해야 합니다.*/
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = spt_find_page(&thread_current()->spt,va);
	
	/* TODO: Fill this function */
	/*
	할당할 페이지를 확보합니다 va.
	먼저 페이지를 가져온 다음, 
	해당 페이지를 사용하여 vm_do_claim_page 함수를 호출해야 합니다.
	*/
	if (page == NULL){
		return false;
	}
	// 실제 페이지 claim 실행
	return vm_do_claim_page (page);
}

/* 
클레임은 물리적 프레임, 즉 페이지를 할당하는 것을 의미합니다.
먼저 vm_get_frame(템플릿에서 이미 수행되어 있는) 함수를 호출하여 프레임을 가져옵니다.
그런 다음 MMU를 설정해야 합니다. 
다시 말해, 페이지 테이블에서 가상 주소와 물리적 주소 간의 매핑을 추가해야 합니다. 
반환 값은 작업의 성공 여부를 나타내야 합니다.
*/
static bool
vm_do_claim_page (struct page *page) {
	// 프레임 가져옴
	struct frame *frame = vm_get_frame ();

	/* Set links
	   페이지 <-> 프레임 연결
	   가상 페이지와 물리 프레임 관게 설정 */
	frame->page = page;
	page->frame = frame;

	struct thread *current = thread_current();
	/* 
	pml4_set_page 함수 인자
	현재 스레드 페이지 테이블, 가상 주소, 물리 주소(커널 가상주소), 쓰기 권한
	pml4_set_page (uint64_t *pml4, void *upage, void *kpage, bool rw) 
	 */
	// 페이지 테이블 엔트리 설정 및 mmu 매핑
	if (!pml4_set_page(current->pml4, page->va, frame->kva, page->rw)){
		// 매핑 실패 처리 - 메모리 부족, 이미 매핑된 주소, 잘못된 권한
		page->frame = NULL;
		// get frame에서 palloc get page했으니 
		palloc_free_page(frame->kva);
		// get frame에서 malloc을 했으니 
		free(frame);

		return false;
	}
	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	// 실제 페이지 내용 로딩
	if (!swap_in (page, frame->kva)){
		// 로딩 실패 - 매핑 끊고 프레임 해제 (아직 테이블에 없음)
		pml4_clear_page(current->pml4, page->va);
		page->frame = NULL;
		palloc_free_page(frame->kva);
		free(frame);
		return false;
	}
	/* 프레임 테이블에 등록 - 페이지가 완전히 준비된 뒤에만 evict 대상이 됨 */
	frame_table_insert(&frame->frame_elem);
	return true;
}

unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED) {
  const struct page *p = hash_entry (p_, struct page, hash_elem);
  return hash_bytes (&p->va, sizeof p->va);
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
		if(!hash_init(&spt->hash_list, page_hash, page_less, NULL)){
			return ;
		}
}

bool
supplemental_page_table_copy (struct supplemental_page_table *dst,
		struct supplemental_page_table *src) {
	
	struct hash_iterator i;

	hash_first (&i, &src->hash_list);

	while (hash_next (&i)) {
		struct page *parent_page = hash_entry (hash_cur (&i), struct page, hash_elem);
		enum vm_type type = VM_TYPE (parent_page->operations->type);

		if (type == VM_UNINIT) { // 여기서 uninit
			struct uninit_page *parent_uninit = &parent_page->uninit;
			struct vm_entry *child_entry = NULL;

			if (parent_uninit->aux != NULL) {
				child_entry = malloc (sizeof (struct vm_entry));
				if (child_entry == NULL)
					return false;
				memcpy (child_entry, parent_uninit->aux, sizeof (struct vm_entry));
			}

			if (!vm_alloc_page_with_initializer (parent_uninit->type, parent_page->va,
					parent_page->rw, parent_uninit->init, child_entry)) {
				free (child_entry);
				return false;
			}
			struct page *child_page = spt_find_page (dst, parent_page->va);
			if (child_page == NULL)
				return false;
			child_page->num = parent_page->num;
		}
		else if (type == VM_ANON) {
			if (parent_page->frame == NULL)
				return false;

			if (!vm_alloc_page (VM_ANON, parent_page->va, parent_page->rw))
				return false;
			
			struct page *child_page = spt_find_page (dst, parent_page->va);
			
			if (child_page == NULL || !vm_do_claim_page (child_page))
				return false;

			memcpy (child_page->frame->kva, parent_page->frame->kva, PGSIZE);
			child_page->num = parent_page->num;
		}
		else if (type == VM_FILE) {
			
			continue;
		}
	}
	return true;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	// buckets에 달린 페이지를 삭제
	// buckets 해제는 hash에서 페이지 해제는 page_destructor에서 
	hash_clear (&spt->hash_list, page_destructor);
}

bool 
page_less(const struct hash_elem *elem1, const struct hash_elem *elem2, void *aux UNUSED){
    const struct page *a = hash_entry(elem1, struct page, hash_elem);
    const struct page *b = hash_entry(elem2, struct page, hash_elem);

	if (a->va < b->va) return true;
	return false;
	
}
// spt 비울때 페이지 하나씩 해제
void 
page_destructor(struct hash_elem *e, void *aux UNUSED){
	struct page *p = hash_entry (e, struct page, hash_elem);
	vm_dealloc_page(p);
}

void frame_table_insert(struct list_elem *elem){
	// struct thread *cur = thread_current();
	// list_push_back(&frame_table, elem);
	// return;
	lock_acquire(&frame_table_lock);
	list_push_back(&frame_table, elem);
	lock_release(&frame_table_lock);
}

// void
// frame_table_remove(struct frame *frame){
// 	lock_acquire(&frame_table_lock);
// 	list_remove(&frame->frame_elem);
// 	lock_release(&frame_table_lock);
// }
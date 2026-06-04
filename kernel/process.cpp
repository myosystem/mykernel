#include "kernel/process.h"
#include "util/memory.h"
#include "util/util.h"
#include "debug/log.h"
#define PHYS_TO_HHDM(pa) ((void *)((uint64_t)(pa) + HHDM_BASE))
// todo - GPT를 코어마다 따로 둘 수 있도록 나중에 페이지기반으로 코어개수만큼 생성해줘야함
alignas(16) static uint8_t gdt[128];
static GDTR gdtr;
static TSS64 tss;
__attribute__((noinline, optimize("O0")))
static uint64_t make_tss_desc0(uint64_t base, uint32_t limit) {
    return (limit & 0xFFFFULL)
        | ((base & 0xFFFFFFFFULL) << 16)
        | (0x89ULL << 40)
        | ((uint64_t)((limit >> 16) & 0xF) << 48)
        | (((base >> 24) & 0xFFULL) << 56);
}
__attribute__((optimize("O0"), noinline))
void init_tss(uint64_t kernel_stack_phys, uint64_t ist1_phys) {
    memset(gdt, 0, sizeof(gdt));

    auto set_desc = [](uint8_t* dst, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags) {
        dst[0] = limit & 0xFF;
        dst[1] = (limit >> 8) & 0xFF;
        dst[2] = base & 0xFF;
        dst[3] = (base >> 8) & 0xFF;
        dst[4] = (base >> 16) & 0xFF;
        dst[5] = access;
        dst[6] = ((limit >> 16) & 0x0F) | (flags & 0xF0);
        dst[7] = (base >> 24) & 0xFF;
    };

    // Kernel Code/Data/User Code/Data
    set_desc(&gdt[8 * 1], 0, 0, 0x9A, 0x20); // Kernel code
    set_desc(&gdt[8 * 2], 0, 0, 0x92, 0x00); // Kernel data
    set_desc(&gdt[8 * 3], 0, 0, 0xFA, 0x20); // User code
    set_desc(&gdt[8 * 4], 0, 0, 0xF2, 0x00); // User data

    memset(&tss, 0, sizeof(tss));
    tss.rsp0 = kernel_stack_phys;
    tss.ist1 = ist1_phys;
    tss.iomap_base = 0xFFFF;

    uint64_t base = (uint64_t)&tss;
    uint32_t limit = sizeof(TSS64) - 1;
    uart_print(base);
    uart_print("\n");
    volatile uint64_t desc[2]{0, 0};
    asm volatile ("" ::: "memory");
    desc[0] = make_tss_desc0(base, limit);
    asm volatile ("" ::: "memory");
    uart_print_hex(desc[0]);
    uart_print("\n");
    desc[1] = ((base >> 32) & 0xFFFFFFFFULL);
    uart_print(desc[1]);
    asm volatile("" : : "m"(desc[0]), "m"(desc[1]) : "memory");
    uart_print("\n");
    memset(((uint8_t*)&desc[1]) + 4, 0, 4); // 상위 32비트 0으로 초기화
    memcpy(&gdt[8 * 5], (void*)desc, 16);

    char* ch = (char*)desc;
    uart_print_hex(desc[0]);
    uart_print("\n");
    for (int i = 0; i < 16; i++) {
        uart_print_hex2(*ch);
        ch++;
    }
    gdtr.base = (uint64_t)&gdt;
    gdtr.limit = sizeof(gdt) - 1;

    __asm__ __volatile__(
        "lgdt %0\n\t"
        "mov ax, 0x10\n\t"
        "mov ds, ax\n\t"
        "mov es, ax\n\t"
        "mov ss, ax\n\t"
        "mov fs, ax\n\t"
        "mov gs, ax\n\t"
        "push 0x08\n\t"
        "lea rax, [rip + 1f]\n\t"
        "push rax\n\t"
        "retfq\n\t"
        "1:\n\t"
        : : "m"(gdtr));
    uint16_t tss_sel = 0x28;
    asm volatile("ltr %0" : : "m"(tss_sel));
}
void* mmap_entry::operator new(size_t size) noexcept {
    mmap_entry* new_entry = (mmap_entry*)MMAP_ENTRY_BASE;
    while (new_entry->flags & MMAP_USED) {
        new_entry++;
    }
    new_entry->flags = MMAP_USED;
	return new_entry;
}
void mmap_entry::operator delete(void* ptr) {
    if (ptr == nullptr) return;
    mmap_entry* entry = (mmap_entry*)ptr;
    entry->flags = 0; // 사용 중이 아님으로 표시
}
queue<size_t>* process_queue;
uint8_t* process_queue_buf[sizeof(queue<size_t>)];

HeapTree<KEvent>* time_event;
uint8_t* time_event_buf[sizeof(HeapTree<KEvent>)];

vector<KEvent>* xhci_event;
uint8_t* xhci_event_buf[sizeof(vector<KEvent>)];

Process* now_process = 0;

void pinit(void* obj) {
	Process* process = (Process*)obj;
    // 한 번만
    process->pallocator = new (process->allocator_buffer) VirtPageAllocator();
    process->cr3 = phy_page_allocator->alloc_phy_page() + HHDM_BASE;
    process->kernel_stack_phys = phy_page_allocator->alloc_phy_page() + PageSize;
    process->mmap_table = nullptr;

    uint64_t lcr3 = virt_page_allocator->getCr3() + HHDM_BASE;
    memset((void*)(process->cr3), 0, PageSize / 2);                              // 하위 절반 초기화
    memcpy((void*)(process->cr3 + 256ull * 8), (void*)(lcr3 + 256ull * 8), 256ull * 8); // 커널 공간 복사
    process->pallocator->init(phy_page_allocator, process->cr3);
}
Process::Process(uint64_t cs, uint64_t ss, Partition* partition,
    uint64_t cwd_cluster, bool full_init) {

    code_va_base = 0x400000;
    time_slice = 100;
    user_stack_bottom = 0x00007FFFFFF000;
    user_stack_top = user_stack_bottom - PageSize * 4;

    kernel_stack = (uint64_t*)(kernel_stack_phys + HHDM_BASE);
    *(--kernel_stack) = ss;
    *(--kernel_stack) = user_stack_bottom;
    *(--kernel_stack) = 0x202;
    *(--kernel_stack) = cs;
    *(--kernel_stack) = (uint64_t)code_va_base;
    for (int i = 0; i < 15; i++) *(--kernel_stack) = 0;
    for (int i = 0; i < 4; i++) *(--kernel_stack) = ss;

    current_partition = partition;
    this->cwd_cluster = cwd_cluster;
    state = 1;
    parent = -1ull;

    if (full_init) {
        open_files.push_back(new STDIn());
        open_files.push_back(new STDOut());
        open_files.push_back(new STDOut());
    }
}
Process::~Process() {
    for (size_t i = 0; i < open_files.get_size(); i++)
        ((File*)open_files[i])->close();

    pallocator->free_all_low_pages(); // 하위 날리면 cr3 하위도 깨끗해짐

    mmap_entry* entry = mmap_table;
    while (entry) {
        mmap_entry* next = entry->next;
        delete entry;
        entry = next;
    }
    mmap_table = nullptr;

    for (uint64_t i = 0; i < children.size(); i++) {
        Process* child = GetProcess(children[i]);
        if (child->parent == id) {
            child->parent = (uint64_t)-1;
            if (child->state & PROCESS_STATE_ZOMBIE)
                operator delete(child);
        }
    }

    state |= PROCESS_STATE_ZOMBIE;
}

void pdestroy(void* obj) {
    Process* process = (Process*)obj;
    phy_page_allocator->put_page(process->cr3 - HHDM_BASE);
    phy_page_allocator->put_page(process->kernel_stack_phys - PageSize - HHDM_BASE);
}
void Process::addCode(void* code_addr) {
    uint64_t code = phy_page_allocator->alloc_phy_page();
    memcpy((void*)(code + HHDM_BASE), code_addr, PageSize);
    pallocator->alloc_virt_page(code_va_base, code, VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::US);
    code_va_base += PageSize;
}
void Process::setHeap() {
    heap_bottom = code_va_base;
	heap_top = code_va_base;    // 힙은 비어있는 상태로 시작
}
void init_process() {
    process_queue = new (process_queue_buf) queue<size_t>();
	time_event = new (time_event_buf) HeapTree<KEvent>((void*)0xFFFF840000000000);
    xhci_event = new (xhci_event_buf) vector<KEvent>;
}
void add_process(size_t index) {
	if (index == IDLE_PROCESS_PID) {
        return; // idle은 큐에 넣지 않음
    }
    process_queue->enqueue(index);
}
Process* GetProcess(size_t index) {
    Process* result = (Process*)Process::get(index);
    return result == nullptr ? idle_process : result;
}
Process* next_process() {
	if (process_queue->isEmpty()) return idle_process;
    uint64_t index = process_queue->dequeue();
    if (index > get_max_process_id()) {
		return next_process(); // 범위 초과된 PID는 건너뛰기
    }
	Process* result = GetProcess(index);
	if (result == nullptr) {
		return next_process(); // 존재하지 않는 프로세스는 건너뛰기
    }
    if ((result->state & 0b1) == 0) {
		return next_process(); // 할당되지 않은 프로세스는 건너뛰기
    }
    return result;
}
__attribute__((noreturn))
void Process::run_process() {
    //uart_print("\nnow_process addr:");
	//uart_print_hex((uint64_t)this);
    tss.rsp0 = this->kernel_stack_phys + HHDM_BASE;
    uint64_t now_rsp = (uint64_t)this->kernel_stack;
	//uart_print("\nSwitching to process PID ");
	//uart_print(this->id);
    //uart_print("\nvirt:");
	//uart_print_hex((uint64_t)&virt_page_allocator);
    /*
    uart_print("\nkernel_stack: ");
    uart_print_hex((uint64_t)this->kernel_stack);

    // kernel_stack이 가리키는 곳의 내용도 출력
    // context_t 구조체 맨 끝(iretq가 읽을 부분): RIP, CS, RFLAGS, RSP, SS
    context_t* ctx = (context_t*)this->kernel_stack;
    uart_print("\nRIP: ");
    uart_print_hex(ctx->rip);
    uart_print("\nCS: ");
    uart_print_hex(ctx->cs);
    uart_print("\nRFLAGS: ");
    uart_print_hex(ctx->rflags);
    uart_print("\nRSP: ");
    uart_print_hex(ctx->rsp);
    */
    virt_page_allocator = this->pallocator;
    virt_page_allocator->setCr3();
    __asm__ __volatile__ (
        "mov rsp, %[now_rsp]\n\t"
        "pop rax\n\t"
        "mov gs, ax\n\t"
        "pop rax\n\t"
        "mov fs, ax\n\t"
        "pop rax\n\t"
        "mov es, ax\n\t"
        "pop rax\n\t"
        "mov ds, ax\n\t"
        "pop r15\n\t"
        "pop r14\n\t"
        "pop r13\n\t"
        "pop r12\n\t"
        "pop r11\n\t"
        "pop r10\n\t"
        "pop r9\n\t"
        "pop r8\n\t"
        "pop rbp\n\t"
        "pop rdi\n\t"
        "pop rsi\n\t"
        "pop rdx\n\t"
        "pop rcx\n\t"
        "pop rbx\n\t"
        "pop rax\n\t"
        "iretq\n\t"
        :
        : [now_rsp]"a"(now_rsp)
    );
    __builtin_unreachable();
}
mmap_entry* Process::isAddrInMMap(uint64_t va) const {
    mmap_entry* entry = mmap_table;
    while (entry) {
        if (entry->va_start <= va && va <= entry->va_end) {
            return entry;
        }
        entry = entry->next;
    }
    return nullptr;
}
volatile uint32_t mmap_lock = 0;
inline void _lockmmap() {
    while (__atomic_test_and_set(&mmap_lock, __ATOMIC_ACQUIRE)) {
		__asm__ __volatile__ ("pause");
    }
}
inline void _unlockmmap() {
    __atomic_clear(&mmap_lock, __ATOMIC_RELEASE);
}
uint64_t Process::mmap(uint64_t size, uint64_t flags, uint64_t arg) {
	if (size == 0) return ~0ULL;
	_lockmmap();
    uint64_t page_count = (size + PageSize - 1) / PageSize;
    mmap_entry* entry = mmap_table;
	mmap_entry* last = nullptr;
    while (entry != nullptr) {
        if (last == nullptr) {
            if (user_stack_top - entry->va_end > page_count * PageSize) {
                break;
            }
        }
        else {
            if (last->va_start - entry->va_end > page_count * PageSize) {
                break;
            }
        }
        last = entry;
        entry = entry->next;
    }
    if (entry == nullptr)
        if (last != nullptr)
            if (last->va_start - (page_count * PageSize) < heap_top) {
                _unlockmmap();
                return ~0ULL; // 할당 불가
			}
    mmap_entry* new_entry = new mmap_entry();
    if (last) {
        last->next = new_entry;
		new_entry->next = entry;
        new_entry->va_end = last->va_start - 1;
		new_entry->va_start = last->va_start - page_count * PageSize;
        new_entry->arg = arg;
		new_entry->flags |= flags;
    } else {
        mmap_table = new_entry;
        mmap_table->next = entry;
		mmap_table->va_end = user_stack_top - 1;
		mmap_table->va_start = user_stack_top - page_count * PageSize;
		mmap_table->arg = arg;
		mmap_table->flags |= flags;
    }
	_unlockmmap();
	return new_entry->va_start;
}
bool Process::munmap(uint64_t va, uint64_t size) {
    if (size == 0) return false;
    _lockmmap();
    uint64_t page_count = (size + PageSize - 1) / PageSize;
    mmap_entry* entry = mmap_table;
    mmap_entry* last = nullptr;
    while (entry != nullptr) {
        if (entry->va_start <= va && va <= entry->va_end) {
            break;
        }
        last = entry;
        entry = entry->next;
    }
    if (entry == nullptr || va + page_count * PageSize - 1 > entry->va_end) {
        _unlockmmap();
        return false; // 해당 영역이 mmap된 영역과 일치하지 않음
    }
    if (entry->va_start == va && entry->va_end == va + page_count * PageSize - 1) {
        // 완전히 일치하는 경우, 엔트리를 제거
        if (last) {
            last->next = entry->next;
        } else {
            mmap_table = entry->next;
        }
		delete entry;
    } else if (entry->va_start == va) {
        // 시작 부분이 일치하는 경우, 시작 주소를 조정
        entry->va_start += page_count * PageSize;
    } else if (entry->va_end == va + page_count * PageSize - 1) {
        // 끝 부분이 일치하는 경우, 끝 주소를 조정
        entry->va_end -= page_count * PageSize;
    } else {
        // 중간 부분이 일치하는 경우, 엔트리를 분할
        mmap_entry* new_entry = (mmap_entry*)MMAP_ENTRY_BASE;
        while (new_entry->flags & MMAP_USED) {
            new_entry++;
        }
        new_entry->flags = entry->flags; // 기존 엔트리의 플래그 복사
        new_entry->va_start = va + page_count * PageSize;
        new_entry->va_end = entry->va_end;
        new_entry->arg = entry->arg; // 기존 엔트리의 arg 복사
        new_entry->next = entry->next;
		entry->va_end = va - 1; // 기존 엔트리의 끝
        entry->next = new_entry;
	}
    _unlockmmap();
	return true;
}
void Process::msg_recv(msg_t msg,bool blocking) {
    if (state & PROCESS_STATE_MSGWAIT) {
        state &= ~PROCESS_STATE_MSGWAIT; // 메시지 대기 상태 해제
        state &= ~PROCESS_STATE_WAITING; // 대기 상태 해제
        add_process(id); // 프로세스 스케줄링 큐에 추가
    }
    if (msg.type == MSG_MOUSE_MOVE) {
        msg_t* last = msgq.peek_back();
        if (last && last->type == MSG_MOUSE_MOVE) {
            last->payload.params.arg[0] = msg.payload.params.arg[0];
            last->payload.params.arg[1] = msg.payload.params.arg[1];
            return;  // enqueue 없이 그냥 업데이트만
        }
    }
    if (msgq.get_size() > ((msg.sender_pid == ((uint64_t)-1)) ? MAX_MESSAGE_QUEUE_INT : MAX_MESSAGE_QUEUE_SIZE)) {
        if (blocking) {
			waiting_msgq.enqueue(msg.sender_pid); // 메시지 보낸 프로세스 PID 대기 큐에 추가
            call_msg_block();
        }
        else {
			return; // 큐가 가득 찼으면 메시지 버리기
        }
    }
    msgq.enqueue(msg);
}
bool Process::msg_pop(msg_t* msg) {
    if (msgq.isEmpty()) {
        return false;
    }
	*msg = msgq.dequeue();
    return true;
}
bool Process::msg_empty() const {
    return msgq.isEmpty();
}
uint64_t Process::fork() {
    Process* child = new Process(0x1B, 0x23, this->current_partition, this->cwd_cluster, false);
    memcpy(child->kernel_stack, this->kernel_stack, sizeof(context_t));
    ((context_t*)child->kernel_stack)->rax = 0; // 자식 프로세스에서는 fork의 반환값이 0
	((context_t*)child->kernel_stack)->rip = ((context_t*)this->kernel_stack)->rip; // 실행 위치는 부모와 동일
    if (!child->pallocator->copy(*pallocator, 0x400000, code_va_base - 0x400000)) { // 코드 복사
        delete child;
		return ~0ULL; // 복사 실패 시 -1 반환
    }
    child->code_va_base = this->code_va_base;
    child->heap_bottom = this->heap_bottom;
    child->heap_top = this->heap_top;
	if (!child->pallocator->copy(*pallocator, heap_top, heap_bottom - heap_top)) { // 힙 복사
        delete child;
		return ~0ULL; // 복사 실패 시 -1 반환
    }
    child->user_stack_bottom = this->user_stack_bottom;
	child->user_stack_top = this->user_stack_top;
	if (!child->pallocator->copy(*pallocator, user_stack_top, user_stack_bottom - user_stack_top)) { // 사용자 스택 복사
        delete child;
		return ~0ULL; // 복사 실패 시 -1 반환
    }
	mmap_entry* entry = this->mmap_table;
	mmap_entry* last_child_entry = nullptr;
	while (entry != nullptr) {
        mmap_entry* new_entry = new mmap_entry();
        new_entry->flags = entry->flags;
        new_entry->va_start = entry->va_start;
        new_entry->va_end = entry->va_end;
        new_entry->arg = entry->arg;
		new_entry->next = nullptr;
		if (!(new_entry->flags & MMAP_SHARED)) {
            if (!child->pallocator->copy(*pallocator, entry->va_start, entry->va_end - entry->va_start + 1)) { // mmap된 영역 복사
                delete child;
                return ~0ULL; // 복사 실패 시 -1 반환
            }
        }
        if (last_child_entry) {
            last_child_entry->next = new_entry;
        } else {
            child->mmap_table = new_entry;
        }
        last_child_entry = new_entry;
        entry = entry->next;
    }
	for (size_t i = 0; i < open_files.get_size(); i++) {
        File* f = (File*)open_files[i];
        f->open();
		child->open_files.push_back(f); // 열린 파일 포인터 공유 (참조 카운트 증가)
    }
	child->parent = this->id;
    this->children.push_back(child->id);
	process_queue->enqueue(child->id);
	return child->id;
}
uint64_t Process::exec(const char* path, const char* argv[], context_t* ctx) {
    File* file = vfs_open(path, current_partition, cwd_cluster);
    if (file == nullptr) {
        return ~0ULL; // 파일 열기 실패 시 -1 반환
    }
    // 1. 유저 공간 날리기
    pallocator->free_all_low_pages();

    // 2. 새 바이너리 로딩
    code_va_base = 0x400000;
    time_slice = 100;
    user_stack_bottom = 0x00007FFFFFF000;
    user_stack_top = user_stack_bottom - PageSize * 4;

    current_partition = file->get_partition();
    this->cwd_cluster = file->get_file_id();
    state = 1;
    uint64_t readbuffer = phy_page_allocator->alloc_phy_page() + HHDM_BASE;
    while (file->read((void*)readbuffer, PageSize) != 0) {
        addCode((void*)readbuffer);
    }
    phy_page_allocator->put_page(readbuffer - HHDM_BASE);
    delete file;

    // 3. 힙 리셋
    setHeap();

    // 4. open_files 정리 (stdin/stdout/stderr 유지)
	while (open_files.get_size() > 3) {
		File* f = (File*)open_files[3];
		f->close();
		open_files.erase(3);
	}

    // 5. 커널 스택 프레임 리셋
    memset((void*)ctx, 0, sizeof(context_t));
	ctx->rip = 0x400000; // 새 코드의 시작 주소
	ctx->rsp = user_stack_top; // 새 사용자 스택의 시작 주소
	ctx->rflags = 0x202; // 인터럽트 허용
    ctx->cs = 0x1B; // 사용자 코드 세그먼트
	ctx->ss = 0x23; // 사용자 데이터 세그먼트
    ctx->fs = ctx->ss;
	ctx->gs = ctx->ss;
	ctx->ds = ctx->ss;
	ctx->es = ctx->ss;
	return 0; // exec 성공 시 0 반환
}
uint64_t get_process_count() {
    return Process::get_count();
}
uint64_t get_max_process_id() {
    return Process::max();
}
__attribute__((naked))
void idle_process_func() {   // 최대한 메모리를 안먹기 위해 
    __asm__ __volatile__(
        "1:\n\t"
        "hlt\n\t"
        "jmp 1b\n\t"
    );
}
Process* idle_process;
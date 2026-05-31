#ifndef __PROCESS_H__
#define __PROCESS_H__
#include "util/size.h"
#include "mm/allocator"
#include "filesys/partition.h"
#include "util/queue.h"
#include "util/heaptree.h"
#include "util/vector.h"
#include "filesys/file.h"

#define MMAP_ENTRY_BASE 0xFFFF808000000000ULL
#define MESSAGE_QUEUE_BASE 0xFFFF810000000000ULL
#define PROCESS_QUEUE_BASE 0xFFFF818000000000ULL

#define PROCESS_STATE_READY     0b1
#define PROCESS_STATE_WAITING   0b10
#define PROCESS_STATE_MSGWAIT   0b100
#define PROCESS_STATE_ZOMBIE    0b1000
struct TSS64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed, aligned(16)));

struct GDTEntry64 {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed));

struct GDTR {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

__attribute__((optimize("O0"), noinline))
void init_tss(uint64_t kernel_stack_phys, uint64_t ist1_phys);
struct mmap_entry {
    uint64_t va_start;
    uint64_t va_end;
    uint64_t flags;         // 1 used/free, 2 r, 3 w, 4 shared
    uint64_t arg;
    mmap_entry* next;
    mmap_entry() {};
    ~mmap_entry() {};
	void* operator new(size_t size) noexcept;
	void operator delete(void* ptr);
};
struct sigaction {
    void     (*sa_handler)(int);           // 핸들러 함수
    //void     (*sa_sigaction)(int, siginfo_t*, void*); // 상세 핸들러
    uint64_t   sa_mask;                    // 핸들러 실행 중 블록할 시그널
    int        sa_flags;                   // 옵션
    void     (*sa_restorer)(void);         // 내부용, 쓰지 말 것
};
struct Event {
    sigaction action;
    uint64_t time;      // 언제 발생할지
    uint64_t interval;  // 0이면 일회성, 아니면 반복
    uint64_t process_id;
	uint64_t type;      // 이벤트 타입 (예: 타이머, IO 등)
};
struct KEvent {
    uint64_t time;      // 언제 발생할지
    uint64_t interval;  // 0이면 일회성, 아니면 반복
    uint64_t process_id;
    uint64_t type;      // 이벤트 타입 (예: 타이머, IO 등)
	uint64_t arg[4];   // 이벤트에 필요한 추가 정보 (예: IO 디바이스 번호, 파일 디스크립터 등)
    uint64_t event_id;
    void (*callback)(void* event_info,uint64_t status, uint64_t control, void* ctx);
    void* callback_ctx;
};
#define EVENT_TYPE_TIMER 1
#define EVENT_TYPE_SLEEP 2
static inline bool operator<(const Event& a, const Event& b) {
    return a.time < b.time;
}
static inline bool operator<(const KEvent& a, const KEvent& b) {
    return a.time < b.time;
}
#define MMAP_USED 1
#define MMAP_READ 2
#define MMAP_WRITE 4
#define MMAP_SHARED 8
enum {
    MSG_NONE = 0,

	MSG_KEY_BASE = 0x100,
	MSG_KEY_PRESS = MSG_KEY_BASE + 1,
	MSG_KEY_RELEASE = MSG_KEY_BASE + 2,

	MSG_TIMER_BASE = 0x200,
	MSG_TIMER_EXPIRED = MSG_TIMER_BASE + 1,
    // generic IPC
    MSG_IPC_BASE = 0x1000,

    // shared memory
    MSG_SHM_BASE = 0x2000,

    // GUI
    MSG_GUI_BASE = 0x3000,
    MSG_MAKE_WINDOW = MSG_GUI_BASE + 1,
    MSG_DESTROY_WINDOW = MSG_GUI_BASE + 2,
    MSG_DRAW_FRAME = MSG_GUI_BASE + 3,
    MSG_MOUSE_MOVE = MSG_GUI_BASE + 0x100,
    MSG_MOUSE_LCLICK = MSG_GUI_BASE + 0x101,
    MSG_MOUSE_RCLICK = MSG_GUI_BASE + 0x102,
    MSG_MOUSE_SCROLL = MSG_GUI_BASE + 0x103,
	MSG_MOUSE_LRELEASE = MSG_MOUSE_LCLICK + 0x10,
	MSG_MOUSE_RRELEASE = MSG_MOUSE_RCLICK + 0x10,
};
typedef struct {
    uint64_t sender_pid;    // 8 bytes (Explicitly aligned)
    uint32_t type;          // 4 bytes
    uint32_t status;        // 4 bytes (타입 옆에 상태나 플래그를 두어 8바이트 쌍을 맞춤)

    union {
        // 일반적인 정수형 인자들
        struct {
            uint64_t arg[3];
        } params;

        // 공유 메모리나 문자열 처리를 위한 구조
        struct {
            void* addr;
            uint64_t size;
            uint64_t flags;
        } memory;

        // 데이터 페이로드 (짧은 메시지 직송용)
        uint8_t raw[24];
    } payload;

    uint64_t timestamp;     // 8 bytes
} __attribute__((packed)) msg_t;
#define MAX_MESSAGE_QUEUE_SIZE 128
#define MAX_MESSAGE_QUEUE_INT 256
class Process {
private:
    queue<msg_t> msgq;
	queue<uint64_t> waiting_msgq; // 메시지 대기 중인 프로세스들의 PID 저장
	vector<uint64_t> children; // 자식 프로세스들의 PID 저장
public:
    uint64_t cr3;
    uint64_t kernel_stack_phys;
    uint64_t user_stack_bottom;
    uint64_t user_stack_top;
    uint64_t state; // always 1
    uint64_t time_slice;
    uint64_t parent;
    VirtPageAllocator* pallocator;
    uint8_t allocator_buffer[sizeof(VirtPageAllocator)];
    uint64_t code_va_base;
    uint64_t* kernel_stack;
    uint64_t heap_top;
    uint64_t heap_bottom;
    mmap_entry* mmap_table;
    uint64_t process_id;
    Partition* current_partition;
	uint64_t cwd_cluster;
    pointer_vector open_files;
    Process() {};
    ~Process();
    void init(uint64_t cs, uint64_t ss, Partition* partition, uint64_t cwd_cluster, bool full_init = true);
    void addCode(void* code_addr);
    void setHeap();
    mmap_entry* isAddrInMMap(uint64_t va) const;
    uint64_t mmap(uint64_t size, uint64_t flags, uint64_t arg);
	bool munmap(uint64_t va, uint64_t size);
    void msg_recv(msg_t msg, bool blocking);
    bool msg_pop(msg_t* msg);
	bool msg_empty() const;
    void run_process();
    void run_process(uint64_t zombie_page);
    uint64_t fork();
	uint64_t exec(const char* path, const char* argv[]);
    
    void* operator new(size_t size);
    void operator delete(void* ptr);
};
extern queue<size_t>* process_queue;   //todo - queue를 코어 개수에 맞게 생성할 수 있도록 확장 필요
extern HeapTree<KEvent>* time_event;
extern vector<KEvent>* xhci_event;
extern Process* now_process;
#define IDLE_PROCESS_PID ((unsigned long)-1)
extern Process* idle_process;
void init_process();
Process* GetProcess(size_t index);
void add_process(size_t process_id);
Process* next_process();
uint64_t get_process_count();
uint64_t get_max_process_id();
#endif /*__PROCESS_H__*/
#ifndef __PROCESS_H__
#define __PROCESS_H__
#include "kernel.h"
#include "size.h"
#include "allocator"

#define MMAP_ENTRY_BASE 0xFFFF808000000000ULL
#define MESSAGE_QUEUE_BASE 0xFFFF810000000000ULL
#define PROCESS_QUEUE_BASE 0xFFFF818000000000ULL

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
    uint64_t flags;
    mmap_entry* next;
};
struct process_message_node {
    process_message_node* next;
    uint64_t flags;
    char message[PageSize - sizeof(next) * 2];
} __attribute__((packed));
class Process {
private:
    process_message_node* message_queue_head;
    process_message_node* message_queue_tail;
public:
    uint64_t cr3;
    uint64_t kernel_stack_phys;
    uint64_t user_stack_bottom;
    uint64_t user_stack_top;
    Process* next;
    uint64_t state; // always 1
    VirtPageAllocator* pallocator;
    uint8_t allocator_buffer[sizeof(VirtPageAllocator)];
    uint64_t code_va_base;
    uint64_t* kernel_stack;
    uint64_t heap_top;
    uint64_t heap_bottom;
    mmap_entry* mmap_table;
    uint64_t process_id;
    Process() = default;
	~Process() = default;
    void init(uint64_t cs, uint64_t ss);
    void addCode(void* code_addr);
    void setHeap();
    bool isAddrInMMap(uint64_t va) const;
    uint64_t mmap(uint64_t size, uint64_t flags);
    void msg_recv(const char* msg, uint64_t flags);
    bool msg_pop(char* out_msg, uint64_t& out_flags);
    void* operator new(size_t size);
    void operator delete(void* ptr);
};
extern Process* now_process;
void init_process(Process* p);
void jmp_process();
void add_process(Process* p);
uint64_t get_process_count();
#endif /*__PROCESS_H__*/
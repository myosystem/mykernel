#include "kernel/kernel.h"
#include "util/memory.h"
#include "mm/allocator"
#include "util/util.h"
#include "kernel/console.h"
#include "debug/log.h"
#include "arch/idt.h"
#include "arch/ioapic.h"
#include "arch/lapic.h"
#include "arch/handler.h"
#include "kernel/timer_handler.h"
#include "arch/pci.h"
#include "filesys/gpt.h"
#include "filesys/FAT32.h"
#include "driver/disk.h"
#include "kernel/process.h"

//pml4
//256 partition data heap
//257 mmap table heap
//258 process message queue
//259 process queue
//260 shared memory info queue
//261 disk queue
//262 Partitioner queue
//263 HANDLE queue
//509 mmio
//510 HHDM
//511 kernel + bootdata + init stack


// 최종 APIC 초기화
void init_apic() {
    disable_pic();               // 1. PIC 끄고
    init_ioapic_base();
    init_lapic_base();
    enable_apic();               // 2. Local APIC 켜고
    setup_lapic_timer(32);
    ioapic_set_redirection(1, 0x21, 0);
    ioapic_set_redirection(12, 0x2C, 0);
}

void init_interrupts() {
	init_allocators(bootinfo->physbm, bootinfo->physbm_size);
    init_apic();
    for (int i = 0; i < IDT_SIZE; i++) {
        set_idt_gate(i, (uint64_t)none_handler, 0x08, 0x8E);
    }
	set_idt_gate(13, (uint64_t)general_protection_fault_handler, 0x08, 0x8E);
	set_idt_gate(14, (uint64_t)page_fault_handler, 0x08, 0x8E);
	set_idt_gate(12, (uint64_t)stack_segment_fault_handler, 0x08, 0x8E);

    set_idt_gate(32, (uint64_t)timer_handler, 0x08, 0x8E);
    set_idt_gate(33, (uint64_t)keyboard_handler, 0x08, 0x8E);
    //set_idt_gate(0x2C, (uint64_t)dummy_mouse_handler, 0x08, 0x8E);
	set_idt_gate(0x80, (uint64_t)syscall_idthandler, 0x08, 0xEE);
    load_idt();
	//asm volatile ("sti");
}
char testbuf[PageSize * 3 + 1];

//일단 콘솔부터
extern "C" __attribute__((force_align_arg_pointer, noinline)) void main() {
    __asm__ __volatile__ ("cli");
    uart_init();
    init_tss(0, 0);
    init_interrupts();
	*(char*)(0xFFFF800000000000) = 0; // pml4 페이지 강제 할당(256번째 엔트리)
	*(char*)(0xFFFF808000000000) = 0; // pml4 페이지 강제 할당(257번째 엔트리)
	*(char*)(0xFFFF810000000000) = 0; // pml4 페이지 강제 할당(258번째 엔트리)
	*(char*)(0xFFFF818000000000) = 0; // pml4 페이지 강제 할당(259번째 엔트리)
	*(char*)(0xFFFF820000000000) = 0; // pml4 페이지 강제 할당(260번째 엔트리)
	*(char*)(0xFFFF828000000000) = 0; // pml4 페이지 강제 할당(261번째 엔트리)
	*(char*)(0xFFFF830000000000) = 0; // pml4 페이지 강제 할당(262번째 엔트리)
	*(char*)(0xFFFF838000000000) = 0; // pml4 페이지 강제 할당(263번째 엔트리)
	Disk* maindisk = new Disk(
        bootinfo->bootdev.pci_bus,
        bootinfo->bootdev.pci_slot,
        bootinfo->bootdev.pci_func);
	maindisk->init();
	Partitioner* main_partitioner = Partitioner::create_default();
    main_partitioner->init(maindisk);
	File* task_file = kernel_open_file("#0/TASK.O");
    
	uint64_t readbuffer = phy_page_allocator->alloc_phy_page() + HHDM_BASE;
    Process* process = new Process();
    process->init(0x1B, 0x23);
    while (task_file->read((void*)readbuffer, PageSize) != 0) { //한페이지씩 읽기
        process->addCode((void*)readbuffer);                    //읽은 내용 옮기기
    }
	process->setHeap();
    init_process();
    now_process = process;
    delete task_file;
    phy_page_allocator->free_phy_page(readbuffer - HHDM_BASE);
    uart_print("\ntest\n");
    virt_page_allocator->free_all_low_pages();
    process->run_process();
}
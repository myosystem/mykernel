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

    HBA_MEM* hba = pci_init();
    uint64_t buf_phys = phy_page_allocator->alloc_phy_page() + MMIO_BASE;
	virt_page_allocator->alloc_virt_page(buf_phys, buf_phys - MMIO_BASE, VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::PCD);
	memset((void*)buf_phys, 0, PageSize);
    ahci_read(hba->ports + bootinfo->bootdev.port_or_ns, 1, 1, (void*)buf_phys);
    FAT32 fs;
	fs.init(init_gpt(hba->ports, (void*)buf_phys), 0, (void*)buf_phys);
    uint32_t filesize = fs.get_file_size("TASK.O");
	uart_print("filesize=");
	uart_print(filesize);
    uart_print("\n");
	uint64_t readbuffer = phy_page_allocator->alloc_phy_page() + HHDM_BASE;
	uint64_t toread = filesize;
	uint32_t offset = 0;
    while (toread > 0) {
        uint32_t chunk = (toread > PageSize) ? PageSize : toread;
        fs.read_file("TASK.O", (void*)(readbuffer + offset), offset, chunk);
        toread -= chunk;
        offset += chunk;
		uart_print("read ");
		uart_print(chunk);
		uart_print(" bytes\n");

	}
	//virt_page_allocator->free_all_low_pages();
    Process* process = new Process();
    process->init(0x1B, 0x23);
    process->addCode((void*)readbuffer);
	process->setHeap();
    init_process(process);
    uart_print("\ntest\n");
    jmp_process();
}
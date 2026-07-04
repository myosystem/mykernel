#include "kernel/kernel.h"
#include "util/memory.h"
#include "mm/allocator"
#include "util/util.h"
#include "kernel/console.h"
#include "debug/log.h"
#include "arch/idt.h"
#include "arch/msr.h"
#include "arch/ioapic.h"
#include "arch/lapic.h"
#include "arch/handler.h"
#include "arch/ahci_c.h"
#include "kernel/timer_handler.h"
#include "arch/pci.h"
#include "filesys/gpt.h"
#include "filesys/FAT32.h"
#include "driver/disk.h"
#include "driver/ahci.h"
#include "driver/nvme.h"
#include "kernel/process.h"
#include "arch/xhci_c.h"

//pml4
//256 partition data heap       0xFFFF800000000000
//257 mmap table heap           0xFFFF808000000000
//258 process message queue     0xFFFF810000000000
//259 process queue             0xFFFF818000000000
//260 shared memory queue       0xFFFF820000000000
//261 disk queue                0xFFFF828000000000
//262 Partitioner queue         0xFFFF830000000000
//263 File queue                0xFFFF838000000000
//264 event heap tree array     0xFFFF840000000000
//265 controller queue          0xFFFF848000000000
//266 xhci queue                0xFFFF850000000000
//267 protocol queue	        0xFFFF858000000000
//268 HID queue                 0xFFFF860000000000
//509 mmio
//510 HHDM
//511 kernel + bootdata + init stack


// 최종 APIC 초기화
void init_apic() {
    disable_pic();               // 1. PIC 끄고
    init_ioapic_base();
    init_lapic_base();
    enable_apic();               // 2. Local APIC 켜고
    tsc_init();
    setup_lapic_timer_tsc_deadline(32);
    ioapic_set_redirection(1, 0x21, 0);
    ioapic_set_redirection(12, 0x2C, 0);
//    enable_cursor();
//	enable_keyboard();
}

void init_interrupts() {
	init_allocators(bootinfo->physbm, bootinfo->refcount, bootinfo->physbm_size);
    init_apic();
    for (int i = 0; i < IDT_SIZE; i++) {
        set_idt_gate(i, (uint64_t)none_handler, 0x08, 0x88);
    }
	set_idt_gate(13, (uint64_t)general_protection_fault_handler, 0x08, 0x8E);
	set_idt_gate(14, (uint64_t)page_fault_handler, 0x08, 0x8E);
	set_idt_gate(12, (uint64_t)stack_segment_fault_handler, 0x08, 0x8E);

    set_idt_gate(32, (uint64_t)timer_handler, 0x08, 0x8E);
    set_idt_gate(33, (uint64_t)keyboard_handler, 0x08, 0x8E);
    set_idt_gate(0x2C, (uint64_t)mouse_handler, 0x08, 0x8E);
    set_idt_gate(0x35, (uint64_t)xhci_handler, 0x08, 0x8E);
	set_idt_gate(0x80, (uint64_t)syscall_idthandler, 0x08, 0xEE);
	set_idt_gate(0x81, (uint64_t)waiting_idthandler, 0x08, 0xEE);
    load_idt();
}
vector<Controller*>* controllers;
uint8_t controller_buf[sizeof(vector<Controller*>)];
vector<Disk*>* disks;
uint8_t disk_buf[sizeof(vector<Disk*>)];
bool booting = true;
//일단 콘솔부터
bool g_pmc_ok = false;
void setup_cpu() {
    uint64_t cr4;
    __asm__ __volatile__("mov %0, cr4" : "=r"(cr4));
    cr4 |= (1u << 7);   // CR4.PGE: enable global pages
    __asm__ __volatile__("mov cr4, %0" :: "r"(cr4));
    // PMU FIXED_CTR1 = unhalted core cycles (guest exec cycles; frozen during host preempt)
    uint32_t pa, pb, pc, pd;
    __asm__ __volatile__("cpuid" : "=a"(pa), "=b"(pb), "=c"(pc), "=d"(pd) : "a"(0xA), "c"(0));
    (void)pb; (void)pc;
    if ((pa & 0xFF) >= 2 && (pd & 0x1F) >= 2) {
        wrmsr(0x38D, rdmsr(0x38D) | 0x30);            // IA32_FIXED_CTR_CTRL: FIXED_CTR1 OS+USR
        wrmsr(0x38F, rdmsr(0x38F) | (1ull << 33));    // IA32_PERF_GLOBAL_CTRL: enable FIXED_CTR1
        g_pmc_ok = true;
    }
    uart_print(g_pmc_ok ? "pmc ok\n" : "pmc off\n");
}
uint64_t get_cycles() {
    return g_pmc_ok ? rdmsr(0x30A) : rdtsc_get();       // IA32_FIXED_CTR1
}
extern "C" __attribute__((force_align_arg_pointer, noinline)) void main() {
    __asm__ __volatile__ ("cli");
    uart_init();
    setup_cpu();
    init_tss(0, 0);
    init_interrupts();
    File* trampoline = kernel_open_file("#0/EFI/BOOT/signal.o");
    init_process();
    
	controllers = new (controller_buf) vector<Controller*>();
	disks = new (disk_buf) vector<Disk*>();
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint16_t slot = 0; slot < 32; slot++) {
            // 펑션 0번 먼저 확인
            if (pci_read16(bus, slot, 0, 0x00) == 0xFFFF) continue;

            // 멀티펑션 여부 확인 (Header Type bit7)
            uint8_t header = pci_read8(bus, slot, 0, 0x0E);
            uint8_t max_func = (header & 0x80) ? 8 : 1;

            for (uint16_t func = 0; func < max_func; func++) {
                if (pci_read16(bus, slot, func, 0x00) == 0xFFFF) continue;
				uint8_t class_code = pci_read8(bus, slot, func, 0x0B);
                uint8_t subclass = pci_read8(bus, slot, func, 0x0A);
				if (class_code == 0x01 && subclass == 0x06) {
                    // AHCI Controller
                    auto* ctrl = new AHCIController(bus, slot, func);
                    controllers->push_back(ctrl);
                }
                else if (class_code == 0x01 && subclass == 0x08) {
                    // NVMe Controller
                    auto* disk = new NVMeDisk(bus, slot, func, 0); // port_or_ns=0 (NVMe는 포트 구분 없음)
                    disk->init();
                    if (disk->is_vaild())
                        disks->push_back(disk);
                }
                else if (class_code == 0x0C && subclass == 0x03) {
                    uint8_t prog_if = pci_read8(bus, slot, func, 0x09);
                    if (prog_if == 0x30) {
                        // XHCI 발견
                        auto* ctrl = new XHCIController(bus, slot, func);
                        controllers->push_back(ctrl);
                    }
                    else if (prog_if == 0x20) {
                        // EHCI 발견
                    }
                }
            }
        }
    }
	for (size_t i = 0; i < controllers->size(); i++) {
        (*controllers)[i]->on_disk_found = static_cast<void(*)(Disk*)>([](Disk* disk) {
            if (disk->is_vaild())
                disks->push_back(disk);
		});
        (*controllers)[i]->init();
    }
	for (size_t i = 0; i < disks->size(); i++) {
        if ((*(*disks)[i]) == bootinfo->bootdev) {
                Partitioner* part = Partitioner::create_default();
                part->init((*disks)[i]);
        }
    }
    for (size_t i = 0; i < disks->size(); i++) {
        //(*disks)[i]->init();
        if ((*(*disks)[i]) != bootinfo->bootdev) {
            Partitioner* part = Partitioner::create_default();
            part->init((*disks)[i]);
        }
    }
	uint64_t readbuffer = phy_page_allocator->alloc_phy_page() + HHDM_BASE;
#ifndef TEST_MODE
    File* display_file = kernel_open_file("#0/DISPLAY.O");
    Process* display = new Process(0x1B, 0x23, (Partition*)PARTITION_QUEUE_BASE, display_file->get_file_id());
    while (display_file->read((void*)readbuffer, PageSize) != 0) { //한페이지씩 읽기
        display->addCode((void*)readbuffer);                    //읽은 내용 옮기기
    }
    display->setHeap();
    delete display_file;
	add_process(display->id);
    
#else
    File* test_file = kernel_open_file("#0/TEST.O");
    Process* test = new Process(0x1B, 0x23, (Partition*)PARTITION_QUEUE_BASE, test_file->get_file_id());
    while (test_file->read((void*)readbuffer, PageSize) != 0) { //한페이지씩 읽기
        test->addCode((void*)readbuffer);                    //읽은 내용 옮기기
    }
    test->setHeap();
    delete test_file;
    add_process(test->id);
#endif
    
    phy_page_allocator->put_page(readbuffer - HHDM_BASE);
	lapic_tsc_deadline_set_ms(10);
    booting = false;
    now_process = next_process();
    now_process->run_process();
}
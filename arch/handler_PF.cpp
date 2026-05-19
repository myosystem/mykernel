#include "kernel/kernel.h"
#include "arch/handler.h"
#include "util/util.h"
#include "kernel/console.h"
#include "debug/log.h"
#include "util/memory.h"
#include "mm/allocator"
#include "kernel/process.h"
#include "mm/shm.h"

static uint8_t console[100 * 40] = { 0, }; // 디버깅용 콘솔 버퍼
__attribute__((interrupt))
void page_fault_handler(interrupt_frame_t* frame, uint64_t error_code) {
    uint64_t cr2;
    __asm__ __volatile__("mov %0, cr2" : "=r"(cr2));
    if (!(error_code & (1ull << 2ull))) {
        uint64_t pml4_entry = (cr2 >> 39) & 0x1FF;
        if (256 <= pml4_entry && pml4_entry <= 268) {
            virt_page_allocator->alloc_virt_page(cr2 & ~0xFFFULL, phy_page_allocator->alloc_phy_page(), VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::G);
            memset((void*)(cr2 & ~0xFFFULL), 0, PageSize);
            return;
        }
    }
    if (now_process) {
        if (error_code & 1) { // Present인데 PF = 권한 위반
            uint64_t pte = virt_page_allocator->get_mapping(cr2 & ~0xFFFULL);
            if (pte != ~0ULL && (pte & VirtPageAllocator::PTE_COW)) {
                // CoW 처리
                uint64_t pa = pte & PTE_ADDR_MASK;
                uint64_t saved_flags = (pte >> 60) & 0x7;
                uint64_t new_flags = VirtPageAllocator::P | VirtPageAllocator::US;
                if (saved_flags & 0x2) new_flags |= VirtPageAllocator::RW;
                if (saved_flags & 0x4) new_flags |= VirtPageAllocator::NX;
                if (phy_page_allocator->get_refcount(pa) == 1) {
                    // 나만 쓰는 페이지, 그냥 권한 복구
                    virt_page_allocator->change_flags(cr2 & ~0xFFFULL, new_flags);
                }
                else {
                    // 복사
                    uint64_t new_pa = phy_page_allocator->alloc_phy_page();
                    memcpy((void*)(new_pa + HHDM_BASE), (void*)(pa + HHDM_BASE), PageSize);
                    phy_page_allocator->put_page(pa);
					virt_page_allocator->free_virt_page(cr2 & ~0xFFFULL);
                    virt_page_allocator->alloc_virt_page(cr2 & ~0xFFFULL, new_pa, new_flags);
                }
                return;
            }
        }
        if (now_process->user_stack_top <= cr2 && cr2 < now_process->user_stack_bottom) {
            virt_page_allocator->alloc_virt_page(cr2 & ~0xFFFULL, phy_page_allocator->alloc_phy_page(), VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::US);
            memset((void*)(cr2 & ~0xFFFULL), 0, PageSize);
            return;
            //uart_print("user stack allocation for ");
            //uart_print_hex(cr2);
            //uart_print("\n");
        }
        else if (now_process->heap_top <= cr2 && cr2 < now_process->heap_bottom) {
            virt_page_allocator->alloc_virt_page(cr2 & ~0xFFFULL, phy_page_allocator->alloc_phy_page(), VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::US);
            memset((void*)(cr2 & ~0xFFFULL), 0, PageSize);
            return;
            //uart_print("user heap allocation for ");
            //uart_print_hex(cr2);
            //uart_print("\n");
        }
        mmap_entry* mmap_ent = now_process->isAddrInMMap(cr2);
        if (mmap_ent != nullptr) {
            uint64_t flags = VirtPageAllocator::P | VirtPageAllocator::US;
            uint64_t mem = 0;
            if (mmap_ent->flags & MMAP_WRITE)
                flags |= VirtPageAllocator::RW;
            if (mmap_ent->flags & MMAP_SHARED) {
                SharedMem* shm = get_shared_mem(mmap_ent->arg);
                mem = shm->phy_pages[(cr2 - (mmap_ent->va_start & ~0xFFFULL)) / PageSize];
                if (mem == 0) {
                    mem = phy_page_allocator->alloc_phy_page();
                    shm->phy_pages[(cr2 - (mmap_ent->va_start & ~0xFFFULL)) / PageSize] = mem;
                }
                else {
                    phy_page_allocator->get_page(mem);
                }
            }
            else {
                mem = phy_page_allocator->alloc_phy_page();
            }
            virt_page_allocator->alloc_virt_page(cr2 & ~0xFFFULL, mem, flags);
            memset((void*)(cr2 & ~0xFFFULL), 0, PageSize);
            //uart_print("user mmap allocation for ");
            //uart_print_hex(cr2);
            //uart_print("\n");
            return;
        }
    }
    uart_print("Page Fault at CR2=");
    uart_print_hex(cr2);
    uart_print("\nRIP=");
    uart_print_hex(frame->rip);
    uart_print("\nError Code=");
    uart_print_hex(error_code);
    uart_print("\nProcess id=");
    uart_print_hex(now_process->process_id);
    uart_print("\n");
    char raw_stack[16];
    memcpy(raw_stack, (void*)&cr2, 8);
    memcpy(raw_stack + 8, (void*)&error_code, 8);
    bytes_to_hex_string(raw_stack, sizeof(raw_stack), (char*)console);
    console[3 * 17] = 'P';
    console[3 * 17 + 1] = 'F';
    __asm__ __volatile__("hlt");
}
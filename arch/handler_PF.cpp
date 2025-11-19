#include "kernel/kernel.h"
#include "arch/handler.h"
#include "util/util.h"
#include "kernel/console.h"
#include "debug/log.h"
#include "util/memory.h"
#include "mm/allocator"
#include "kernel/process.h"

static uint8_t console[100 * 40] = { 0, }; // 디버깅용 콘솔 버퍼
__attribute__((interrupt))
void page_fault_handler(interrupt_frame_t* frame, uint64_t error_code) {
    uint64_t cr2;
    __asm__ __volatile__ ("mov %0, cr2" : "=r"(cr2));
    if (!(error_code & (1ull << 2ull))) {
        switch ((cr2 >> 39) & 0x1FF)
        {
        case 256:
		case 257:
		case 258:
        case 259:
        {
            virt_page_allocator->alloc_virt_page(cr2 & ~0xFFFULL, phy_page_allocator->alloc_phy_page(), VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::G);
            memset((void*)(cr2 & ~0xFFFULL), 0, PageSize);
            uart_print("on-demand page allocation for ");
            uart_print_hex(cr2);
            uart_print("\n");
			return;
        }
        default:
            break;
        }
    }
    if (now_process->user_stack_top <= cr2 && cr2 < now_process->user_stack_bottom) {
        virt_page_allocator->alloc_virt_page(cr2 & ~0xFFFULL, phy_page_allocator->alloc_phy_page(), VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::US);
        memset((void*)(cr2 & ~0xFFFULL), 0, PageSize);
        uart_print("on-demand page allocation for ");
        uart_print_hex(cr2);
        uart_print("\n");
    }
    else if (now_process->heap_top <= cr2 && cr2 < now_process->heap_bottom) {
        virt_page_allocator->alloc_virt_page(cr2 & ~0xFFFULL, phy_page_allocator->alloc_phy_page(), VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::US);
        memset((void*)(cr2 & ~0xFFFULL), 0, PageSize);
        uart_print("on-demand page allocation for ");
        uart_print_hex(cr2);
        uart_print("\n");
    }
    else if (now_process->isAddrInMMap(cr2)) {
        virt_page_allocator->alloc_virt_page(cr2 & ~0xFFFULL, phy_page_allocator->alloc_phy_page(), VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::US);
        memset((void*)(cr2 & ~0xFFFULL), 0, PageSize);
        uart_print("on-demand page allocation for ");
        uart_print_hex(cr2);
        uart_print("\n");
	}
    else {
        char raw_stack[16];
        memcpy(raw_stack, (void*)&cr2, 8);
        memcpy(raw_stack + 8, (void*)&error_code, 8);
        bytes_to_hex_string(raw_stack, sizeof(raw_stack), (char*)console);
        console[3 * 17] = 'P';
        console[3 * 17 + 1] = 'F';
        for (unsigned int i = 0; i < bootinfo->framebufferPitch * bootinfo->framebufferHeight; i++) {
            uint32_t PixelColor = 0xFFFFFF;
            *((uint32_t*)(bootinfo->framebufferAddr) + i) = PixelColor;
        }
        while (1) {
            for (int y = 0; y < 40; y++) {
                for (int x = 0; x < 100; x++) {
                    putc(bootinfo, x * 1 * 8 + 4, y * 2 * 16 + 4, console[y * 100 + x], 0, 1);
                }
            }
            __asm__ __volatile__("hlt");
        }
    }
}
#include "mm/VirtPageAllocator"
#include "util/memory.h"
#include "debug/log.h"
volatile uint32_t vpaspinv = 0;
inline void _lockv() {
    while (__atomic_test_and_set(&vpaspinv, __ATOMIC_ACQUIRE)) {
        __asm__ __volatile__("pause");
    }
}
inline void _unlockv() {
    __atomic_clear(&vpaspinv, __ATOMIC_RELEASE);
}
inline void VirtPageAllocator::invlpg(void* addr) {
    asm volatile("invlpg [%0]" :: "r"(addr) : "memory");
}
inline void VirtPageAllocator::reload_cr3() {
    uint64_t cr3;
    asm volatile ("mov %0, cr3" : "=r"(cr3));
    asm volatile ("mov cr3, %0" :: "r"(cr3) : "memory");
}
VirtPageAllocator::VirtPageAllocator() = default;
uint64_t VirtPageAllocator::getCr3() {
    uint64_t cr3;
    asm volatile ("mov %0, cr3" : "=r"(cr3));
    return cr3;
}
void VirtPageAllocator::setCr3() {
    uint64_t cr3 = (uint64_t)pml4 - HHDM_BASE;
    asm volatile ("mov cr3, %0" :: "r"(cr3));
}
void VirtPageAllocator::init(PhysPageAllocator* phy_allocator, uint64_t pml4) {
    this->phy_allocator = phy_allocator;
    if (!pml4) {
        pml4 = VirtPageAllocator::getCr3() & ~0xFFFULL;
        pml4 += HHDM_BASE;
    }
    this->pml4 = (void*)pml4;
}
uint64_t VirtPageAllocator::alloc_virt_page(uint64_t va, uint64_t pa, uint64_t flags) {
    if ((va & 0xFFF) || (pa & 0xFFF)) return ~0ULL; // 정렬 불량
    uart_print("va\n0x");
    uart_print_hex(va);
    uart_print("\n0x");
    uart_print_hex(pa);
    uart_print("\n");
    _lockv();
    if (va == pa) {
        uart_print("identical mapping\n");
    }
    uint64_t us = va & (1ULL << 63) ? 0 : US;
    // PML4E
    uint64_t* pml4e = (uint64_t*)pml4 + ((va >> 39) & 0x1FF);
    if (!(*pml4e & P)) {
        uint64_t new_pdpt_pa = phy_allocator->alloc_phy_page();
        if (!new_pdpt_pa) { _unlockv(); return ~0ULL; }
        memset((void*)(HHDM_BASE + new_pdpt_pa), 0, 4096);
        *pml4e = (new_pdpt_pa & ~0xFFFULL) | RW | P | us; // User=0(커널)
    }

    // PDPTE
    uint64_t* pdpte = (uint64_t*)(HHDM_BASE + (*pml4e & ~0xFFFULL)) + ((va >> 30) & 0x1FF);
    if (*pdpte & PS) { _unlockv(); return ~0ULL; } // 1GiB 페이지 충돌
    if (!(*pdpte & P)) {
        uint64_t new_pd_pa = phy_allocator->alloc_phy_page();
        if (!new_pd_pa) { _unlockv(); return ~0ULL; }
        memset((void*)(HHDM_BASE + new_pd_pa), 0, 4096);
        *pdpte = (new_pd_pa & ~0xFFFULL) | RW | P | us;
    }

    // PDE
    uint64_t* pde = (uint64_t*)(HHDM_BASE + (*pdpte & ~0xFFFULL)) + ((va >> 21) & 0x1FF);
    if (*pde & PS) { _unlockv(); return ~0ULL; } // 2MiB 페이지 충돌
    if (!(*pde & P)) {
        uint64_t new_pt_pa = phy_allocator->alloc_phy_page();
        if (!new_pt_pa) { _unlockv(); return ~0ULL; }
        memset((void*)(HHDM_BASE + new_pt_pa), 0, 4096);
        *pde = (new_pt_pa & ~0xFFFULL) | RW | P | us;
    }

    // PTE
    uint64_t* pte = (uint64_t*)(HHDM_BASE + (*pde & ~0xFFFULL)) + ((va >> 12) & 0x1FF);
    if (*pte & P) { _unlockv(); return ~0ULL; } // 이미 매핑됨 (구분하려면 별도 코드 사용)

    *pte = (pa & ~0xFFFULL) | flags;

    invlpg((void*)va); // TLB flush (해당 VA만)

    _unlockv();
    return va;
}
uint64_t VirtPageAllocator::alloc_virt_pages(uint64_t va, uint64_t size, uint64_t flags) {
    if ((va & 0xFFF) || (size & 0xFFF)) return ~0ULL; // 정렬 불량
    uint64_t pages = (size + 4095) / 4096;
    uart_print("size:");
    uart_print(size);
    uart_print("\npages:");
    uart_print(pages);
    uart_print("\n");
    for (uint64_t i = 0; i < pages; i++) {
		uint64_t pa = phy_allocator->alloc_phy_page();
        if (alloc_virt_page(va + i * 4096, pa, flags) == ~0ULL) {
            // 실패 시 지금까지 할당된 페이지 해제
            phy_allocator->free_phy_page(pa);
            uart_print("error!!\n");
            free_virt_pages(va, i * 4096);
            return ~0ULL;
        }
    }
    return va;
}
uint64_t VirtPageAllocator::alloc_virt_pages_range(uint64_t va, uint64_t pa, uint64_t size, uint64_t flags) {
    if ((va & 0xFFF) || (pa & 0xFFF) || (size & 0xFFF)) return ~0ULL; // 정렬 불량
    uint64_t pages = (size + 4095) / 4096;
    uart_print("size:");
    uart_print(size);
    uart_print("\npages:");
    uart_print(pages);
    uart_print("\n");
    for (uint64_t i = 0; i < pages; i++) {
        if (alloc_virt_page(va + i * 4096, pa + i * 4096, flags) == ~0ULL) {
            // 실패 시 지금까지 할당된 페이지 해제
            uart_print("error!!\n");
            free_virt_pages(va, i * 4096);
            return ~0ULL;
        }
    }
    return va;
}
uint64_t VirtPageAllocator::free_virt_page(uint64_t va) {
    if (va & 0xFFF) return ~0ULL; // 정렬 불량
    _lockv();
    // PML4E
    uint64_t* pml4e = (uint64_t*)pml4 + ((va >> 39) & 0x1FF);
    if (!(*pml4e & P)) { _unlockv(); return ~0ULL; } // 미할당
    // PDPTE
    uint64_t* pdpte = (uint64_t*)(HHDM_BASE + (*pml4e & ~0xFFFULL)) + ((va >> 30) & 0x1FF);
    if (!(*pdpte & P)) { _unlockv(); return ~0ULL; } // 미할당
    if (*pdpte & PS) { _unlockv(); return ~0ULL; } // 1GiB 페이지 충돌
    // PDE
    uint64_t* pde = (uint64_t*)(HHDM_BASE + (*pdpte & ~0xFFFULL)) + ((va >> 21) & 0x1FF);
    if (!(*pde & P)) { _unlockv(); return ~0ULL; } // 미할당
    if (*pde & PS) { _unlockv(); return ~0ULL; } // 2MiB 페이지 충돌
    // PTE
    uint64_t* pte = (uint64_t*)(HHDM_BASE + (*pde & ~0xFFFULL)) + ((va >> 12) & 0x1FF);
    if (!(*pte & P)) { _unlockv(); return ~0ULL; } // 미할당
	uint64_t result = *pte & ~0xFFFULL;
    *pte = 0; // 해제
    invlpg((void*)va); // TLB flush (해당 VA만)
    _unlockv();
	return result;
}

void free_pt(VirtPageAllocator* virt, uint64_t* pte_base) {
    for (int i = 0; i < 512; i++) {
        if (pte_base[i] & VirtPageAllocator::P) {
            uint64_t pa = pte_base[i] & ~0xFFFULL;
            virt->phy_allocator->free_phy_page(pa);
            pte_base[i] = 0;
        }
    }
	virt->phy_allocator->free_phy_page((uint64_t)pte_base - HHDM_BASE);
}
void free_pd(VirtPageAllocator* virt, uint64_t* pde_base) {
    for (int i = 0; i < 512; i++) {
        if (pde_base[i] & VirtPageAllocator::P) {
            if (pde_base[i] & VirtPageAllocator::PS) {
                uint64_t pa = pde_base[i] & ~0x1FFFFFULL;
                virt->phy_allocator->free_phy_page(pa);
                pde_base[i] = 0;
            } else {
                uint64_t* pt_base = (uint64_t*)(HHDM_BASE + (pde_base[i] & ~0xFFFULL));
                free_pt(virt, pt_base);
                pde_base[i] = 0;
            }
        }
    }
    virt->phy_allocator->free_phy_page((uint64_t)pde_base - HHDM_BASE);
}
void free_pdpt(VirtPageAllocator* virt, uint64_t* pdpte_base) {
    for (int i = 0; i < 512; i++) {
        if (pdpte_base[i] & VirtPageAllocator::P) {
            if (pdpte_base[i] & VirtPageAllocator::PS) {
                uint64_t pa = pdpte_base[i] & ~0x3FFFFFFFULL;
                virt->phy_allocator->free_phy_page(pa);
                pdpte_base[i] = 0;
            } else {
                uint64_t* pd_base = (uint64_t*)(HHDM_BASE + (pdpte_base[i] & ~0xFFFULL));
                free_pd(virt, pd_base);
                pdpte_base[i] = 0;
            }
        }
    }
    virt->phy_allocator->free_phy_page((uint64_t)pdpte_base - HHDM_BASE);
}
void VirtPageAllocator::free_all_low_pages() {
    _lockv();
    for(int i = 0; i < 256; i++) {
        uint64_t* pml4e = (uint64_t*)pml4 + i;
        if (*pml4e & P) {
            uint64_t* pdpte_base = (uint64_t*)(HHDM_BASE + (*pml4e & ~0xFFFULL));
            free_pdpt(this, pdpte_base);
            *pml4e = 0;
        }
	}
    reload_cr3();
    _unlockv();
}
void VirtPageAllocator::free_virt_pages(uint64_t va, uint64_t size) {
    if (va & 0xFFF || size & 0xFFF) return; // 정렬 불량
    uint64_t pages = (size + 4095) / 4096;
    for (uint64_t i = 0; i < pages; i++) {
        phy_allocator->free_phy_page(free_virt_page(va + i * 4096));
    }
}

uint64_t VirtPageAllocator::get_mapping(uint64_t va) {
    if (va & 0xFFF) return ~0ULL; // 정렬 불량
    _lockv();
    // PML4E
    uint64_t* pml4e = (uint64_t*)pml4 + ((va >> 39) & 0x1FF);
    if (!(*pml4e & P)) { _unlockv(); return ~0ULL; } // 미할당
    // PDPTE
    uint64_t* pdpte = (uint64_t*)(HHDM_BASE + (*pml4e & ~0xFFFULL)) + ((va >> 30) & 0x1FF);
    if (!(*pdpte & P)) { _unlockv(); return ~0ULL; } // 미할당
    if (*pdpte & PS) { 
        uint64_t pa = (*pdpte & ~0x3FFFFFFFULL) | (va & 0x3FFFFFFFULL);
        _unlockv(); 
        return pa; // 1GiB 페이지
    }
    // PDE
    uint64_t* pde = (uint64_t*)(HHDM_BASE + (*pdpte & ~0xFFFULL)) + ((va >> 21) & 0x1FF);
    if (!(*pde & P)) { _unlockv(); return ~0ULL; } // 미할당
    if (*pde & PS) { 
        uint64_t pa = (*pde & ~0x1FFFFFULL) | (va & 0x1FFFFFULL);
        _unlockv(); 
        return pa; // 2MiB 페이지
    }
    // PTE
    uint64_t* pte = (uint64_t*)(HHDM_BASE + (*pde & ~0xFFFULL)) + ((va >> 12) & 0x1FF);
    if (!(*pte & P)) { _unlockv(); return ~0ULL; } // 미할당
    uint64_t pa = (*pte & ~0xFFFULL) | (va & 0xFFF);
    _unlockv();
    return pa;
}
#include "driver/nvme.h"
#include "arch/pci.h"
#include "mm/allocator"
#include "util/memory.h"
#define ADMIN_QUEUE_SIZE 4
#define IO_QUEUE_SIZE    4
volatile uint32_t* get_sq_doorbell(uint8_t* nvme_base, uint64_t stride, uint16_t qid) {
    return (volatile uint32_t*)(nvme_base + 0x1000 + (2ull * qid) * stride);
}

volatile uint32_t* get_cq_doorbell(uint8_t* nvme_base, uint64_t stride, uint16_t qid) {
    return (volatile uint32_t*)(nvme_base + 0x1000 + (2ull * qid + 1) * stride);
}
void NVMeDisk::init() {
    // 1. BAR0 매핑 (동일)
    pci_bar_info_t bar = pci_get_bar_size(pci_bus, pci_slot, pci_func, 0x10);
    nvme_base = (volatile uint8_t*)(bar.addr + MMIO_BASE);
    if (bar.addr > phy_page_allocator->get_total_pages() * PageSize) {
        nvme_base = (volatile uint8_t*)mmio_bump;
        mmio_bump += bar.size;
    }
    uint64_t bar_size = bar.size; // PCI에서 읽어온 실제 크기 (보통 8KB 이상)
    for (uint64_t offset = 0; offset < bar_size; offset += 4096) {
        virt_page_allocator->alloc_virt_page(
            (uint64_t)nvme_base + offset, bar.addr + offset,
            VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::PCD);
    }
    // 2. 각 큐를 위한 독립된 물리 페이지 할당 (총 4개)
    auto alloc_queue = [&](uint64_t& phys, uint64_t& virt) {
        phys = phy_page_allocator->alloc_phy_page(); // 4KiB 정렬된 페이지 반환
        virt = phys + MMIO_BASE;
        virt_page_allocator->alloc_virt_page(virt, phys,
            VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::PCD);
        memset((void*)virt, 0, 4096);
        };

    alloc_queue(asq_phys, asq_virt);   // Admin Submission Queue
    alloc_queue(acq_phys, acq_virt);   // Admin Completion Queue
    alloc_queue(iosq_phys, iosq_virt); // I/O Submission Queue
    alloc_queue(iocq_phys, iocq_virt); // I/O Completion Queue

    // 3. CC.EN=0 리셋
    volatile uint32_t* cc = (volatile uint32_t*)(nvme_base + 0x14);
    volatile uint32_t* csts = (volatile uint32_t*)(nvme_base + 0x1C);

    *cc &= ~1;
    while (*csts & 1); // 리셋 완료 대기

    // 4. Admin Queue 등록 (이제 주소들이 4KiB 단위로 깔끔하게 떨어짐)
    volatile uint32_t* aqa = (volatile uint32_t*)(nvme_base + 0x24);
    volatile uint64_t* asq_reg = (volatile uint64_t*)(nvme_base + 0x28);
    volatile uint64_t* acq_reg = (volatile uint64_t*)(nvme_base + 0x30);

    // 엔트리 개수 설정 (0-based)
    *aqa = ((ADMIN_QUEUE_SIZE - 1) << 16) | (ADMIN_QUEUE_SIZE - 1);
    *asq_reg = asq_phys;
    *acq_reg = acq_phys;

    // 5. CC.EN=1 활성화
    // MPS=0(4KB), CSS=0(NVM Command Set), IOSQES=6(64B), IOCQES=4(16B)
    *cc = (6 << 16) | (4 << 20) | 1;
    while (!(*csts & 1)); // 여기서 이제 안 터질 겁니다!

    // Doorbell 주소 (Stride는 QEMU 기본값인 0이라 가정: 0, 4, 8, 12...)
    uint64_t cap = *(volatile uint64_t*)nvme_base;
    uint32_t dstrd = (cap >> 32) & 0xF;
    stride = 4ull << dstrd;  // 멤버변수에 저장

    volatile uint32_t* adm_sq_db = get_sq_doorbell((uint8_t*)nvme_base, stride, 0);
    volatile uint32_t* adm_cq_db = get_cq_doorbell((uint8_t*)nvme_base, stride, 0);

    // 6. Identify Controller (데이터 버퍼도 별도 페이지)
    uint64_t id_phys = phy_page_allocator->alloc_phy_page();
    uint64_t id_virt = id_phys + MMIO_BASE;
    virt_page_allocator->alloc_virt_page(id_virt, id_phys,
        VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::PCD);
    memset((void*)id_virt, 0, 4096);

    NVMeCmd* asq = (NVMeCmd*)asq_virt;
    volatile NVMeCqe* acq = (volatile NVMeCqe*)acq_virt;

    uint8_t phase = 1;
    uint8_t asq_tail = 0;
    uint8_t acq_head = 0;

    // Identify 명령 전송
    memset(&asq[asq_tail], 0, sizeof(NVMeCmd));
    asq[asq_tail].opc = 0x06;
    asq[asq_tail].cid = asq_tail;
    asq[asq_tail].prp1 = id_phys;
    asq[asq_tail].cdw10 = 1;  // CNS=1: Controller
    *adm_sq_db = ++asq_tail;

    while ((acq[acq_head].status & 1) != phase);
    uint16_t status = acq[acq_head].status >> 1; // Status Code는 상위 15비트
    if (status != 0) {
		uart_print("Identify Controller failed with status: ");
		uart_print_hex(status);
		uart_print("\n");
    }
    acq_head++;
    if (acq_head == ADMIN_QUEUE_SIZE) {
        acq_head = 0;
        phase = !phase; // 1 -> 0 또는 0 -> 1
    }
    *adm_cq_db = acq_head;
    // 7. Create I/O CQ (QID=1)
    memset(&asq[asq_tail], 0, sizeof(NVMeCmd));
    asq[asq_tail].opc = 0x05;
    asq[asq_tail].cid = asq_tail;
    asq[asq_tail].prp1 = iocq_phys;
    asq[asq_tail].cdw10 = ((IO_QUEUE_SIZE - 1) << 16) | 1;
    asq[asq_tail].cdw11 = 1;  // PC=1 (Physically Contiguous)
    *adm_sq_db = ++asq_tail;

    while ((acq[acq_head].status & 1) != phase);
    status = acq[acq_head].status >> 1; // Status Code는 상위 15비트
    if (status != 0) {
        uart_print("Identify Controller failed with status: ");
        uart_print_hex(status);
        uart_print("\n");
    }
    acq_head++;
    if (acq_head == ADMIN_QUEUE_SIZE) {
        acq_head = 0;
        phase = !phase; // 1 -> 0 또는 0 -> 1
    }
    *adm_cq_db = acq_head;

    // 8. Create I/O SQ (QID=1, CQID=1)
    memset(&asq[asq_tail], 0, sizeof(NVMeCmd));
    asq[asq_tail].opc = 0x01;
    asq[asq_tail].cid = asq_tail;
    asq[asq_tail].prp1 = iosq_phys;
    asq[asq_tail].cdw10 = ((IO_QUEUE_SIZE - 1) << 16) | 1;
    asq[asq_tail].cdw11 = (1 << 16) | 1; // CQID=1, PC=1
    *adm_sq_db = ++asq_tail;

    while ((acq[acq_head].status & 1) != phase);
    status = acq[acq_head].status >> 1; // Status Code는 상위 15비트
    if (status != 0) {
        uart_print("Identify Controller failed with status: ");
        uart_print_hex(status);
        uart_print("\n");
    }
    acq_head++;
    if (acq_head == ADMIN_QUEUE_SIZE) {
        acq_head = 0;
        phase = !phase; // 1 -> 0 또는 0 -> 1
    }
    *adm_cq_db = acq_head;

    // 정리 및 상태 저장
    virt_page_allocator->free_virt_page(id_virt);
    phy_page_allocator->put_page(id_phys);

    sq_tail = 0;
    cq_head = 0;
    cq_phase = 1;
    namespace_id = 1;
}
int NVMeDisk::read_sector(uint64_t lba, uint32_t count, void* phys_buf) {
    NVMeCmd* iosq = (NVMeCmd*)iosq_virt;
    volatile NVMeCqe* iocq = (volatile NVMeCqe*)iocq_virt;

    // Doorbell 주소 (I/O Queue는 QID=1이라 +8 오프셋)
    // Doorbell stride는 CAP[35:32]로 결정되지만 보통 4바이트
    volatile uint32_t* sq_db = get_sq_doorbell((uint8_t*)nvme_base, stride, 1);
    volatile uint32_t* cq_db = get_cq_doorbell((uint8_t*)nvme_base, stride, 1);

    // Read 명령 (NVM Command Set, Opcode 0x02)
    memset(&iosq[sq_tail], 0, sizeof(NVMeCmd));
    iosq[sq_tail].opc = 0x02;          // Read
    iosq[sq_tail].cid = sq_tail;
    iosq[sq_tail].nsid = namespace_id;
    iosq[sq_tail].prp1 = (uint64_t)phys_buf;  // 이미 물리주소
    iosq[sq_tail].prp2 = 0;             // 4KB 이하면 불필요
    iosq[sq_tail].cdw10 = (uint32_t)(lba & 0xFFFFFFFF);        // SLBA 하위
    iosq[sq_tail].cdw11 = (uint32_t)(lba >> 32);               // SLBA 상위
    iosq[sq_tail].cdw12 = (count - 1);   // NLB (0-based)

    // Doorbell 울리기
    __asm__ volatile ("sfence" ::: "memory");
    sq_tail = (sq_tail + 1) % IO_QUEUE_SIZE;
    *sq_db = sq_tail;

    // CQ 완료 대기 (Phase bit 체크)
    while ((iocq[cq_head].status & 1) != cq_phase)
        __asm__ volatile ("pause");

    // 에러 체크
    uint16_t status = iocq[cq_head].status >> 1;
    if (status != 0) return -1;

    // CQ Head 업데이트
    cq_head = (cq_head + 1) % IO_QUEUE_SIZE;
    if (cq_head == 0) cq_phase ^= 1;  // 한 바퀴 돌면 phase 반전
    *cq_db = cq_head;

    return 0;
}
int NVMeDisk::write_sector(uint64_t lba, uint32_t count, const void* phys_buf) {
    NVMeCmd* iosq = (NVMeCmd*)iosq_virt;
    volatile NVMeCqe* iocq = (volatile NVMeCqe*)iocq_virt;

    // Doorbell 주소 (I/O Queue는 QID=1이라 +8 오프셋)
    // Doorbell stride는 CAP[35:32]로 결정되지만 보통 4바이트
    volatile uint32_t* sq_db = get_sq_doorbell((uint8_t*)nvme_base, stride, 1);
    volatile uint32_t* cq_db = get_cq_doorbell((uint8_t*)nvme_base, stride, 1);

    // Write 명령 (NVM Command Set, Opcode 0x01)
    memset(&iosq[sq_tail], 0, sizeof(NVMeCmd));
    iosq[sq_tail].opc = 0x01;          // Write
    iosq[sq_tail].cid = sq_tail;
    iosq[sq_tail].nsid = namespace_id;
    iosq[sq_tail].prp1 = (uint64_t)phys_buf;  // 이미 물리주소
    iosq[sq_tail].prp2 = 0;             // 4KB 이하면 불필요
    iosq[sq_tail].cdw10 = (uint32_t)(lba & 0xFFFFFFFF);        // SLBA 하위
    iosq[sq_tail].cdw11 = (uint32_t)(lba >> 32);               // SLBA 상위
    iosq[sq_tail].cdw12 = (count - 1);   // NLB (0-based)

    // Doorbell 울리기
    __asm__ volatile ("sfence" ::: "memory");
    sq_tail = (sq_tail + 1) % IO_QUEUE_SIZE;
    *sq_db = sq_tail;

    // CQ 완료 대기 (Phase bit 체크)
    while ((iocq[cq_head].status & 1) != cq_phase)
        __asm__ volatile ("pause");

    // 에러 체크
    uint16_t status = iocq[cq_head].status >> 1;
    if (status != 0) return -1;

    // CQ Head 업데이트
    cq_head = (cq_head + 1) % IO_QUEUE_SIZE;
    if (cq_head == 0) cq_phase ^= 1;  // 한 바퀴 돌면 phase 반전
    *cq_db = cq_head;

    return 0;
}
NVMeDisk::~NVMeDisk() {
	cleanup();
    virt_page_allocator->free_virt_page(asq_virt);
    phy_page_allocator->put_page(asq_phys);
    virt_page_allocator->free_virt_page(acq_virt);
    phy_page_allocator->put_page(acq_phys);
    virt_page_allocator->free_virt_page(iosq_virt);
    phy_page_allocator->put_page(iosq_phys);
    virt_page_allocator->free_virt_page(iocq_virt);
    phy_page_allocator->put_page(iocq_phys);
    // BAR0는 크기만큼 해제
    uint64_t bar_size = pci_get_bar_size(pci_bus, pci_slot, pci_func, 0x10).size;
    for (uint64_t offset = 0; offset < bar_size; offset += 4096)
        virt_page_allocator->free_virt_page((uint64_t)nvme_base + offset);
}
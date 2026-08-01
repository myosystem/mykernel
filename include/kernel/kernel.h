#ifndef __KERNEL_H__
#define __KERNEL_H__

#include "util/size.h"
typedef struct {
    uint8_t  type;       // 1 = AHCI/SATA, 2 = NVMe, 3 = USB MSC ...
    uint16_t pci_bus;
    uint16_t pci_slot;
    uint16_t pci_func;
    uint32_t port_or_ns;
} boot_device_info_t;
typedef struct {
    uint64_t framebufferAddr;
    uint32_t framebufferWidth;
    uint32_t framebufferHeight;
    uint32_t framebufferPitch;
    uint32_t framebufferFormat;
    uint64_t* physbm;
	uint64_t* refcount;
    uint64_t physbm_size;
    void* rsdp;
    boot_device_info_t bootdev;
} BootInfo;
enum PML4 {
    USER_SPACE = 0,

    PML4_POOL_GUARD_LOW = 255,
    PARTITION_HEAP,
    MMAP_TABLE_HEAP,
    SOCKET_HEAP,
    PROCESS_HEAP,
    SHARED_MEMORY_HEAP,
    DISK_HEAP,
    PARTITIONER_HEAP,
    FILE_HEAP,
    TIME_EVENT_TREE,
    CONTROLLER_HEAP,
    XHCI_HEAP,
    PROTOCOL_HEAP,
    HID_HEAP,
    AML_NODE_HEAP,
    AML_OBJECT_HEAP,
	PML4_POOL_GUARD_HIGH,

    MMIO_SPACE = 509,
    HHDM_SPACE = 510,
    KERNEL_SPACE = 511
};
constexpr uint64_t pml4_addr(uint64_t index) {
    uint64_t addr = index << 39;
    if (index >= 256)
        addr |= 0xFFFF000000000000ULL;  // sign extend
    return addr;
}
extern int cursor_x;
extern int cursor_y;
#define BOOTINFO_VA   0xFFFFFFFF00200000ull
#define bootinfo ((BootInfo*)BOOTINFO_VA)
#endif /* __KERNEL_H__ */
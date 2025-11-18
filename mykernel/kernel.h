#ifndef __KERNEL_H__
#define __KERNEL_H__

#include "size.h"
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
    uint64_t physbm_size;
    void* rsdp;
    boot_device_info_t bootdev;
} BootInfo;
#define BOOTINFO_VA   0xFFFFFFFF00200000ull
#define bootinfo ((BootInfo*)BOOTINFO_VA)
#endif /* __KERNEL_H__ */
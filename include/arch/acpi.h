#ifndef __ACPI_H__
#define __ACPI_H__
#include "util/size.h"
#include "util/vector.h"
#include "util/new.h"

struct RSDP {
    char     signature[8];
    uint8_t  checksum;
    char     oemid[6];
    uint8_t  revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t  ext_checksum;
    uint8_t  reserved[3];
} __attribute__((packed));

struct SDTHeader {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oemid[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct GAS {
    uint8_t  address_space;
    uint8_t  bit_width;
    uint8_t  bit_offset;
    uint8_t  access_size;
    uint64_t address;
} __attribute__((packed));

struct FADT {
    SDTHeader header;               // 0
    uint32_t  firmware_ctrl;        // 36
    uint32_t  dsdt;                 // 40
    uint8_t   reserved0;            // 44
    uint8_t   preferred_pm_profile; // 45
    uint16_t  sci_int;             // 46
    uint32_t  smi_cmd;             // 48
    uint8_t   acpi_enable;         // 52
    uint8_t   acpi_disable;        // 53
    uint8_t   s4bios_req;          // 54
    uint8_t   pstate_cnt;          // 55
    uint32_t  pm1a_evt_blk;        // 56
    uint32_t  pm1b_evt_blk;        // 60
    uint32_t  pm1a_cnt_blk;        // 64
    uint32_t  pm1b_cnt_blk;        // 68
    uint32_t  pm2_cnt_blk;         // 72
    uint32_t  pm_tmr_blk;          // 76
    uint32_t  gpe0_blk;            // 80
    uint32_t  gpe1_blk;            // 84
    uint8_t   pm1_evt_len;         // 88
    uint8_t   pm1_cnt_len;         // 89
    uint8_t   pm2_cnt_len;         // 90
    uint8_t   pm_tmr_len;          // 91
    uint8_t   gpe0_blk_len;        // 92
    uint8_t   gpe1_blk_len;        // 93
    uint8_t   gpe1_base;           // 94
    uint8_t   cst_cnt;             // 95
    uint16_t  p_lvl2_lat;          // 96
    uint16_t  p_lvl3_lat;          // 98
    uint16_t  flush_size;          // 100
    uint16_t  flush_stride;        // 102
    uint8_t   duty_offset;         // 104
    uint8_t   duty_width;          // 105
    uint8_t   day_alrm;            // 106
    uint8_t   mon_alrm;            // 107
    uint8_t   century;             // 108
    uint16_t  iapc_boot_arch;      // 109
    uint8_t   reserved1;           // 111
    uint32_t  flags;               // 112
    GAS       reset_reg;           // 116
    uint8_t   reset_value;         // 128
    uint16_t  arm_boot_arch;       // 129
    uint8_t   fadt_minor;          // 131
    uint64_t  x_firmware_ctrl;     // 132
    uint64_t  x_dsdt;              // 140
    GAS       x_pm1a_evt_blk;      // 148
    GAS       x_pm1b_evt_blk;      // 160
    GAS       x_pm1a_cnt_blk;      // 172
    GAS       x_pm1b_cnt_blk;      // 184
} __attribute__((packed));

void acpi_dump_rsdp();
void acpi_dump_xsdt();
void acpi_dump_fadt();
void acpi_dump_dsdt();
void acpi_aml_probe();

extern SDTHeader* g_fadt;
extern uint64_t g_dsdt;
extern uint32_t g_pm1a_cnt;
extern uint32_t g_pm1b_cnt;
extern uint32_t g_smi_cmd;
extern uint8_t  g_acpi_enable;
extern uint16_t g_slp_typ_a;
extern uint16_t g_slp_typ_b;
extern bool     g_s5_valid;

// ---- AML namespace: NewObject pools (PML4 269/270, demand-mapped by PF handler) ----
// Empty user-provided ctors so `new T()` (value-init) does NOT zero-init the object,
// which would clobber NewObject's state/id set by operator new.
#define AML_NODE_BASE 0xFFFF868000000000ULL // PML4[269]
#define AML_OBJ_BASE  0xFFFF870000000000ULL // PML4[270]

struct AmlObject : public NewObject<AML_OBJ_BASE, 0x100, nullptr, nullptr> {
    uint8_t  type = 0;
    uint64_t integer = 0;
    uint8_t* data = nullptr;
    uint32_t len = 0;
    vector<AmlObject*> package;
    AmlObject() {}
};
static_assert(sizeof(AmlObject) <= 0x100, "AmlObject slot too small");

struct AmlNode : public NewObject<AML_NODE_BASE, 0x200, nullptr, nullptr> {
    char name[5] = {0,0,0,0,0};
    AmlObject value;
    AmlNode*  parent = nullptr;
    vector<AmlNode*> children;
    AmlNode() {}
};
static_assert(sizeof(AmlNode) <= 0x200, "AmlNode slot too small");

void acpi_build_namespace();
void acpi_dump_namespace();
extern AmlNode* g_aml_root;

#endif // __ACPI_H__

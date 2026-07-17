#include "arch/acpi.h"
#include "kernel/kernel.h"
#include "debug/log.h"

#ifndef HHDM_BASE
#define HHDM_BASE 0xFFFFFF0000000000ULL
#endif

// bootinfo->rsdp holds the RSDP physical address (from UEFI config table).
// Physical RAM is mapped at HHDM_BASE, so access via (phys + HHDM_BASE).
void acpi_dump_rsdp() {
    uint64_t phys = (uint64_t)bootinfo->rsdp;
    uart_print("[ACPI] rsdp phys=0x");
    uart_print_hex(phys);
    uart_putc('\n');
    if (phys == 0) { uart_print("[ACPI] rsdp is NULL\n"); return; }

    RSDP* r = (RSDP*)(phys + HHDM_BASE);

    uart_print("[ACPI] sig='");
    for (int i = 0; i < 8; i++) uart_putc(r->signature[i]);
    uart_print("' oemid='");
    for (int i = 0; i < 6; i++) uart_putc(r->oemid[i]);
    uart_print("'\n[ACPI] revision=");
    uart_print((uint64_t)r->revision);
    uart_print(" rsdt=0x");
    uart_print_hex(r->rsdt_address);
    if (r->revision >= 2) {
        uart_print(" xsdt=0x");
        uart_print_hex(r->xsdt_address);
        uart_print(" length=");
        uart_print((uint64_t)r->length);
    }
    uart_putc('\n');

    // verify v1 checksum: sum of first 20 bytes must be 0
    uint8_t sum = 0;
    uint8_t* p = (uint8_t*)r;
    for (int i = 0; i < 20; i++) sum += p[i];
    uart_print("[ACPI] v1 checksum ");
    uart_print(sum == 0 ? "OK\n" : "BAD\n");
}

SDTHeader* g_fadt = nullptr;

static bool sig_is(const char* s, const char* four) {
    return s[0] == four[0] && s[1] == four[1] && s[2] == four[2] && s[3] == four[3];
}

static void dump_one(uint64_t table_phys) {
    SDTHeader* t = (SDTHeader*)(table_phys + HHDM_BASE);
    uart_print("[ACPI]   ");
    for (int j = 0; j < 4; j++) uart_putc(t->signature[j]);
    uart_print(" @ 0x");
    uart_print_hex(table_phys);
    if (sig_is(t->signature, "FACP")) {
        g_fadt = t;
        uart_print("  <- FADT");
    }
    uart_putc('\n');
}

void acpi_dump_xsdt() {
    RSDP* r = (RSDP*)((uint64_t)bootinfo->rsdp + HHDM_BASE);
    g_fadt = nullptr;

    if (r->revision >= 2 && r->xsdt_address) {
        SDTHeader* xsdt = (SDTHeader*)(r->xsdt_address + HHDM_BASE);
        int count = (int)((xsdt->length - sizeof(SDTHeader)) / 8);
        uint8_t* arr = (uint8_t*)xsdt + sizeof(SDTHeader); // 8-byte entries (may be unaligned)
        uart_print("[ACPI] XSDT entries=");
        uart_print((uint64_t)count);
        uart_putc('\n');
        for (int i = 0; i < count; i++) {
            uint64_t ptr;
            uint8_t* e = arr + i * 8;
            for (int b = 0; b < 8; b++) ((uint8_t*)&ptr)[b] = e[b]; // unaligned-safe read
            dump_one(ptr);
        }
    } else {
        SDTHeader* rsdt = (SDTHeader*)((uint64_t)r->rsdt_address + HHDM_BASE);
        int count = (int)((rsdt->length - sizeof(SDTHeader)) / 4);
        uint32_t* arr = (uint32_t*)((uint8_t*)rsdt + sizeof(SDTHeader));
        uart_print("[ACPI] RSDT entries=");
        uart_print((uint64_t)count);
        uart_putc('\n');
        for (int i = 0; i < count; i++) dump_one((uint64_t)arr[i]);
    }

    uart_print("[ACPI] FADT ");
    uart_print(g_fadt ? "found\n" : "NOT FOUND\n");
}

uint64_t g_dsdt       = 0;
uint32_t g_pm1a_cnt   = 0;
uint32_t g_pm1b_cnt   = 0;
uint32_t g_smi_cmd    = 0;
uint8_t  g_acpi_enable = 0;
uint16_t g_slp_typ_a = 0;
uint16_t g_slp_typ_b = 0;
bool     g_s5_valid  = false;

void acpi_dump_fadt() {
    if (!g_fadt) { uart_print("[ACPI] no FADT\n"); return; }
    FADT* f = (FADT*)g_fadt;

    // prefer the 64-bit extended (X_) fields when the firmware provides them
    g_dsdt       = f->x_dsdt ? f->x_dsdt : (uint64_t)f->dsdt;
    g_pm1a_cnt   = f->x_pm1a_cnt_blk.address ? (uint32_t)f->x_pm1a_cnt_blk.address : f->pm1a_cnt_blk;
    g_pm1b_cnt   = f->x_pm1b_cnt_blk.address ? (uint32_t)f->x_pm1b_cnt_blk.address : f->pm1b_cnt_blk;
    g_smi_cmd    = f->smi_cmd;
    g_acpi_enable = f->acpi_enable;

    uart_print("[ACPI] SMI_CMD=0x");    uart_print_hex(g_smi_cmd);
    uart_print(" ACPI_ENABLE=0x");      uart_print_hex((uint64_t)g_acpi_enable);
    uart_print("\n[ACPI] PM1a_CNT=0x"); uart_print_hex(g_pm1a_cnt);
    uart_print(" PM1b_CNT=0x");         uart_print_hex(g_pm1b_cnt);
    uart_print(" PM1_CNT_LEN=");        uart_print((uint64_t)f->pm1_cnt_len);
    uart_print("\n[ACPI] DSDT=0x");     uart_print_hex(g_dsdt);
    uart_print(" (dsdt32=0x");          uart_print_hex((uint64_t)f->dsdt);
    uart_print(" x_dsdt=0x");           uart_print_hex(f->x_dsdt);
    uart_print(")\n");
}

void acpi_dump_dsdt() {
    if (!g_dsdt) { uart_print("[AML] no DSDT\n"); return; }
    SDTHeader* d = (SDTHeader*)(g_dsdt + HHDM_BASE);

    uart_print("[AML] DSDT sig='");
    for (int i = 0; i < 4; i++) uart_putc(d->signature[i]);
    uart_print("' length=");
    uart_print((uint64_t)d->length);

    uint8_t sum = 0;
    uint8_t* p = (uint8_t*)d;
    for (uint32_t i = 0; i < d->length; i++) sum += p[i];
    uart_print(" checksum ");
    uart_print(sum == 0 ? "OK\n" : "BAD\n");

    // AML byte stream begins right after the 36-byte header
    uint8_t*  aml     = (uint8_t*)d + sizeof(SDTHeader);
    uint32_t  aml_len = d->length - (uint32_t)sizeof(SDTHeader);
    uart_print("[AML] body_len=");
    uart_print((uint64_t)aml_len);
    uart_print(" first bytes:\n");

    uint32_t n = aml_len < 48 ? aml_len : 48;
    for (uint32_t i = 0; i < n; i++) {
        if (i % 16 == 0) uart_print("[AML]   ");
        uart_print_hex2(aml[i]);
        uart_putc(' ');
        if (i % 16 == 15) uart_putc('\n');
    }
    if (n % 16 != 0) uart_putc('\n');
}

// --- AML core decoders ---------------------------------------------------

// PkgLength: returns encoded length; advances p past the length bytes.
// byte0 bits7-6 = # of following bytes. If 0: length = byte0 & 0x3F.
// else: length = (byte0 & 0x0F) | (next bytes << 4, 12, 20).
uint32_t aml_pkglength(const uint8_t*& p) {
    uint8_t lead = *p++;
    uint32_t cnt = lead >> 6;
    if (cnt == 0) return lead & 0x3F;
    uint32_t len = lead & 0x0F;
    for (uint32_t i = 0; i < cnt; i++) len |= (uint32_t)(*p++) << (4 + 8 * i);
    return len;
}

// NameString: writes readable form to out; advances p past it.
// prefixes: '\'(0x5C root), '^'(0x5E parent). path: NullName(0x00),
// DualNamePrefix(0x2E)+2 segs, MultiNamePrefix(0x2F)+count+N segs, or 1 NameSeg.
void aml_namestring(const uint8_t*& p, char* out) {
    int oi = 0;
    while (*p == 0x5C || *p == 0x5E) { out[oi++] = (*p == 0x5C) ? '\\' : '^'; p++; }
    if (*p == 0x00) { p++; if (oi == 0) out[oi++] = '\\'; out[oi] = 0; return; }
    int segs = 1;
    if (*p == 0x2E) { p++; segs = 2; }
    else if (*p == 0x2F) { p++; segs = *p++; }
    for (int s = 0; s < segs; s++) {
        if (s) out[oi++] = '.';
        for (int j = 0; j < 4; j++) out[oi++] = (char)*p++;
    }
    out[oi] = 0;
}

void acpi_aml_probe() {
    if (!g_dsdt) { uart_print("[AML] no DSDT\n"); return; }
    SDTHeader* d = (SDTHeader*)(g_dsdt + HHDM_BASE);
    const uint8_t* p = (const uint8_t*)d + sizeof(SDTHeader);

    uint8_t op = *p++;
    uart_print("[AML] first op=0x");
    uart_print_hex2(op);
    if (op == 0x10) {
        uint32_t len = aml_pkglength(p);
        char name[64];
        aml_namestring(p, name);
        uart_print(" ScopeOp pkglen=");
        uart_print((uint64_t)len);
        uart_print(" name='");
        uart_print(name);
        uart_print("'\n");
    } else {
        uart_print(" (expected 0x10 ScopeOp)\n");
    }
}

// ===================== Step C: AML namespace tree ========================

AmlNode* g_aml_root = nullptr;
static const uint8_t* g_aml_base = nullptr; // for diagnostic offsets

static AmlNode* new_node(AmlNode* parent, const char* seg4) {
    AmlNode* n = new AmlNode();
    for (int i = 0; i < 4; i++) n->name[i] = seg4[i];
    n->name[4] = 0;
    n->parent = parent;
    if (parent) parent->children.push_back(n);
    return n;
}
static AmlNode* find_child(AmlNode* p, const char* seg4) {
    for (size_t i = 0; i < p->children.size(); i++) {
        AmlNode* c = p->children[i];
        if (c->name[0]==seg4[0] && c->name[1]==seg4[1] && c->name[2]==seg4[2] && c->name[3]==seg4[3])
            return c;
    }
    return nullptr;
}
static AmlNode* find_or_create(AmlNode* p, const char* seg4) {
    AmlNode* c = find_child(p, seg4);
    return c ? c : new_node(p, seg4);
}

// advance p past a NameString without touching the tree
static void skip_namestring(const uint8_t*& p) {
    while (*p == 0x5C || *p == 0x5E) p++;
    if (*p == 0x00) { p++; return; }
    int segs;
    if (*p == 0x2E) { p++; segs = 2; }
    else if (*p == 0x2F) { p++; segs = *p++; }
    else segs = 1;
    p += segs * 4;
}

// navigate from `cur` to the parent scope of the final NameSeg; copy final seg to out_seg.
// null path (e.g. "\") -> returns that node, out_seg[0]=0.
static AmlNode* aml_navigate(AmlNode* cur, const uint8_t*& p, char* out_seg) {
    AmlNode* node = cur;
    while (*p == 0x5C || *p == 0x5E) {
        if (*p == 0x5C) node = g_aml_root;
        else if (node->parent) node = node->parent;
        p++;
    }
    int segs;
    if (*p == 0x00) { p++; out_seg[0] = 0; return node; }
    else if (*p == 0x2E) { p++; segs = 2; }
    else if (*p == 0x2F) { p++; segs = *p++; }
    else segs = 1;
    for (int s = 0; s < segs; s++) {
        char seg[4]; for (int j = 0; j < 4; j++) seg[j] = (char)*p++;
        if (s == segs - 1) { for (int j = 0; j < 4; j++) out_seg[j] = seg[j]; out_seg[4] = 0; return node; }
        node = find_or_create(node, seg);
    }
    return node;
}

static uint16_t rd16(const uint8_t* p){ return (uint16_t)(p[0] | (p[1]<<8)); }
static uint32_t rd32(const uint8_t* p){ return (uint32_t)(p[0] | (p[1]<<8) | (p[2]<<16) | ((uint32_t)p[3]<<24)); }
static uint64_t rd64(const uint8_t* p){ uint64_t v=0; for(int i=0;i<8;i++) v|=(uint64_t)p[i]<<(8*i); return v; }

static void parse_dataobject(const uint8_t*& p, const uint8_t* end, AmlObject* o);

static void parse_package(const uint8_t*& p, const uint8_t* end, AmlObject* o) {
    const uint8_t* s = p;
    uint32_t len = aml_pkglength(p);
    const uint8_t* pend = s + len;
    uint8_t num = *p++;
    o->type = 4;
    for (uint8_t i = 0; i < num && p < pend; i++) {
        AmlObject* e = new AmlObject();
        parse_dataobject(p, pend, e);
        o->package.push_back(e);
    }
    p = pend;
}

static void parse_dataobject(const uint8_t*& p, const uint8_t* end, AmlObject* o) {
    if (p >= end) { o->type = 0; return; }
    uint8_t b = *p;
    switch (b) {
        case 0x00: p++; o->type=1; o->integer=0; break;               // Zero
        case 0x01: p++; o->type=1; o->integer=1; break;               // One
        case 0xFF: p++; o->type=1; o->integer=~0ull; break;           // Ones
        case 0x0A: p++; o->type=1; o->integer=*p++; break;            // BytePrefix
        case 0x0B: p++; o->type=1; o->integer=rd16(p); p+=2; break;   // WordPrefix
        case 0x0C: p++; o->type=1; o->integer=rd32(p); p+=4; break;   // DWordPrefix
        case 0x0E: p++; o->type=1; o->integer=rd64(p); p+=8; break;   // QWordPrefix
        case 0x0D: { p++; o->type=2; const uint8_t* st=p; while(p<end && *p) p++; o->len=(uint32_t)(p-st); if(p<end)p++; } break; // String
        case 0x12: p++; parse_package(p, end, o); break;              // Package
        case 0x11: { p++; const uint8_t* s=p; uint32_t l=aml_pkglength(p); o->type=3; p=s+l; } break; // Buffer: skip
        default:
            o->type = 0;
            if (b==0x5C||b==0x5E||b==0x2E||b==0x2F||b=='_'||(b>='A'&&b<='Z')) skip_namestring(p);
            else p++;
            break;
    }
}

static void parse_termlist(const uint8_t*& p, const uint8_t* end, AmlNode* scope) {
    while (p < end) {
        const uint8_t* term = p;
        uint8_t op = *p++;
        if (op == 0x10) {                 // ScopeOp
            const uint8_t* s=p; uint32_t len=aml_pkglength(p); const uint8_t* e=s+len;
            char seg[5]; AmlNode* par=aml_navigate(scope,p,seg);
            AmlNode* t = seg[0] ? find_or_create(par,seg) : par;
            parse_termlist(p, e, t); p = e;
        } else if (op == 0x08) {          // NameOp
            char seg[5]; AmlNode* par=aml_navigate(scope,p,seg);
            AmlNode* n = seg[0] ? find_or_create(par,seg) : par;
            parse_dataobject(p, end, &n->value);
        } else if (op == 0x14) {          // MethodOp (skip body)
            const uint8_t* s=p; uint32_t len=aml_pkglength(p); const uint8_t* e=s+len;
            char seg[5]; AmlNode* par=aml_navigate(scope,p,seg);
            if (seg[0]) find_or_create(par,seg);
            p = e;
        } else if (op == 0x06) {          // Alias
            skip_namestring(p); skip_namestring(p);
        } else if (op == 0x15) {          // External: name, type, argcount
            skip_namestring(p); p += 2;
        } else if (op == 0x5B) {          // extended opcodes
            uint8_t ext = *p++;
            if (ext == 0x82 || ext == 0x85) {          // Device / ThermalZone
                const uint8_t* s=p; uint32_t len=aml_pkglength(p); const uint8_t* e=s+len;
                char seg[5]; AmlNode* par=aml_navigate(scope,p,seg);
                AmlNode* t=seg[0]?find_or_create(par,seg):par;
                parse_termlist(p, e, t); p = e;
            } else if (ext == 0x83) {                  // Processor: +6 fixed bytes
                const uint8_t* s=p; uint32_t len=aml_pkglength(p); const uint8_t* e=s+len;
                char seg[5]; AmlNode* par=aml_navigate(scope,p,seg);
                AmlNode* t=seg[0]?find_or_create(par,seg):par;
                p += 6; parse_termlist(p, e, t); p = e;
            } else if (ext == 0x84) {                  // PowerResource: +3 fixed bytes
                const uint8_t* s=p; uint32_t len=aml_pkglength(p); const uint8_t* e=s+len;
                char seg[5]; AmlNode* par=aml_navigate(scope,p,seg);
                AmlNode* t=seg[0]?find_or_create(par,seg):par;
                p += 3; parse_termlist(p, e, t); p = e;
            } else if (ext == 0x80) {                  // OperationRegion
                char seg[5]; AmlNode* par=aml_navigate(scope,p,seg);
                if (seg[0]) find_or_create(par,seg);
                p += 1;                                 // region space
                AmlObject a, b2; parse_dataobject(p,end,&a); parse_dataobject(p,end,&b2); // offset, len
            } else if (ext == 0x81 || ext == 0x86 || ext == 0x87) { // Field/IndexField/BankField
                const uint8_t* s=p; uint32_t len=aml_pkglength(p); p = s + len; // skip
            } else if (ext == 0x01) {                  // Mutex: name, sync
                skip_namestring(p); p += 1;
            } else if (ext == 0x02) {                  // Event: name
                skip_namestring(p);
            } else {
                uart_print("[AML] unknown ext op 0x"); uart_print_hex2(ext);
                uart_print(" at +"); uart_print((uint64_t)(term - g_aml_base)); uart_print("\n");
                return;
            }
        } else {
            uart_print("[AML] unknown op 0x"); uart_print_hex2(op);
            uart_print(" at +"); uart_print((uint64_t)(term - g_aml_base)); uart_print("\n");
            return;
        }
    }
}

void acpi_build_namespace() {
    if (!g_dsdt) { uart_print("[AML] no DSDT\n"); return; }
    SDTHeader* d = (SDTHeader*)(g_dsdt + HHDM_BASE);
    const uint8_t* p   = (const uint8_t*)d + sizeof(SDTHeader);
    const uint8_t* end = (const uint8_t*)d + d->length;

    g_aml_root = new AmlNode();
    g_aml_root->name[0] = '\\'; g_aml_root->name[1] = 0;
    g_aml_root->parent = nullptr;
    g_aml_base = p;

    parse_termlist(p, end, g_aml_root);
    uart_print("[AML] namespace built (parsed to +");
    uart_print((uint64_t)(p - g_aml_base));
    uart_print(" of ");
    uart_print((uint64_t)(end - g_aml_base));
    uart_print(")\n");
}

static void find_s5(AmlNode* n, int depth) {
    if (!n || depth > 128) return;
    if (n->name[0]=='_' && n->name[1]=='S' && n->name[2]=='5' && n->name[3]=='_') {
        uart_print("[AML] _S5 found, type=");
        uart_print((uint64_t)n->value.type);
        if (n->value.type == 4) {
            uart_print(" pkg size=");
            uart_print((uint64_t)n->value.package.size());
            for (size_t i = 0; i < n->value.package.size() && i < 4; i++) {
                uart_print(" [");  uart_print((uint64_t)i); uart_print("]=0x");
                uart_print_hex(n->value.package[i]->integer);
            }
            if (n->value.package.size() >= 2) {
                g_slp_typ_a = (uint16_t)n->value.package[0]->integer;
                g_slp_typ_b = (uint16_t)n->value.package[1]->integer;
                g_s5_valid  = true;
            }
        }
        uart_putc('\n');
    }
    size_t nc = n->children.size();
    if (nc > 65536) return;
    for (size_t i = 0; i < nc; i++) find_s5(n->children[i], depth + 1);
}

void acpi_dump_namespace() {
    if (!g_aml_root) { uart_print("[AML] no namespace\n"); return; }
    uart_print("[AML] root children=");
    uart_print((uint64_t)g_aml_root->children.size());
    uart_putc('\n');
    find_s5(g_aml_root, 0);
}

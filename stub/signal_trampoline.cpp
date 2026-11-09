asm(R"(
.text
.global sig_entry
.global sig_entry_end
sig_entry:
    call rsi
    mov  rax, 39
    int  0x80
    ud2
sig_entry_end:
)");
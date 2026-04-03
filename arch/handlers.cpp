#include "kernel/kernel.h"
#include "arch/handler.h"
#include "arch/idt.h"
#include "arch/io.h"
#include "util/util.h"
#include "kernel/console.h"
#include "debug/log.h"
#include "arch/lapic.h"
#include "util/memory.h"
#include "kernel/process.h"
#include "kernel/timer_handler.h"
#define min(a, b) ((a) < (b) ? (a) : (b))

//todo - 종속 부분과 비종속부분 분리 후 새로운 파일로 구분
__attribute__((interrupt))
void keyboard_handler(interrupt_frame_t* frame) {
    uint8_t scancode = inb(0x60);
	uart_print("Key pressed: ");
	uart_print("0x");
	uart_print_hex2(scancode);
    uart_print("\n");
    /*
    if (console[0] == 0) {
        char raw_frame[24];
        memcpy(raw_frame, frame, 24);
        bytes_to_hex_string(raw_frame, 24, (char*)console);
    }
    */
    lapic_eoi();
}
static uint8_t mouse_cycle = 0;
static uint8_t mouse_bytes[3];
volatile int cursor_x = 0;
volatile int cursor_y = 0;
__attribute__((interrupt))
void mouse_handler(interrupt_frame_t* frame) {
    mouse_bytes[mouse_cycle] = inb(0x60);

    if (mouse_cycle == 0 && !(mouse_bytes[0] & 0x08)) {
        // 동기화 오류, 무시
    }
    else if (++mouse_cycle == 3) {
        mouse_cycle = 0;

        int dx = mouse_bytes[1];
        int dy = mouse_bytes[2];

        if (mouse_bytes[0] & 0x10) dx -= 256;  // X 부호 보정
        if (mouse_bytes[0] & 0x20) dy -= 256;  // Y 부호 보정

        cursor_x -= dx;
        cursor_y += dy;  // Y축 반전

        // 경계 처리
        if (cursor_x < 0) cursor_x = 0;
        if (cursor_y < 0) cursor_y = 0;
        if (cursor_x >= (int)bootinfo->framebufferWidth) cursor_x = bootinfo->framebufferWidth - 1;
        if (cursor_y >= (int)bootinfo->framebufferHeight) cursor_y = bootinfo->framebufferHeight - 1;
        ((Process*)PROCESS_QUEUE_BASE)->msg_recv({ (-1ull),MSG_MOUSE_MOVE, 0, {(uint64_t)cursor_x, (uint64_t)cursor_y,0} });
    }
    lapic_eoi();
}
__attribute__((naked))
void timer_handler() {
    asm volatile(
        // 인터럽트 진입시 트랩 프레임이 이미 스택에 있음 (rip, cs, rflags, [rsp, ss])
        // 추가적으로 전역/일반 레지스터도 저장
        "push rax\n\t"
        "push rbx\n\t"
        "push rcx\n\t"
        "push rdx\n\t"
        "push rsi\n\t"
        "push rdi\n\t"
        "push rbp\n\t"
        "push r8\n\t"
        "push r9\n\t"
        "push r10\n\t"
        "push r11\n\t"
        "push r12\n\t"
        "push r13\n\t"
        "push r14\n\t"
        "push r15\n\t"
        "mov rax, ds\n\t"
        "push rax\n\t"
        "mov rax, es\n\t"
        "push rax\n\t"
        "mov rax, fs\n\t"
        "push rax\n\t"
        "mov rax, gs\n\t"
        "push rax\n\t"
        // 이제 스택 상태:
        // [r15][r14]...[rax][RIP][CS][RFLAGS]([RSP][SS]) <- rsp

        // 스택 프레임 주소를 첫 번째 인자(rdi)로 넘기자!
        "mov rdi, rsp\n\t"
        // C로 진입! (C에서 스케줄링/컨텍스트스위칭/복원 판단)
        "call c_timer_handler\n\t"
        // 들어온곳 그대로 원상복구 (핸들러에서 진입 안했을경우 그대로 복귀)

        // pop 순서대로 pop
        "pop rax\n\t"
        "mov gs, ax\n\t"
        "pop rax\n\t"
        "mov fs, ax\n\t"
        "pop rax\n\t"
        "mov es, ax\n\t"
        "pop rax\n\t"
        "mov ds, ax\n\t"
        "pop r15\n\t"
        "pop r14\n\t"
        "pop r13\n\t"
        "pop r12\n\t"
        "pop r11\n\t"
        "pop r10\n\t"
        "pop r9\n\t"
        "pop r8\n\t"
        "pop rbp\n\t"
        "pop rdi\n\t"
        "pop rsi\n\t"
        "pop rdx\n\t"
        "pop rcx\n\t"
        "pop rbx\n\t"
        "pop rax\n\t"

        // 이제 트랩 프레임만 스택에 남아 있음
        // iretq로 복귀 (RIP, CS, RFLAGS, [RSP, SS])를 자동으로 pop
        "iretq\n\t"
        );
}
__attribute__((interrupt))
void none_handler(interrupt_frame_t* frame) {
    __asm__ __volatile__("hlt");
    lapic_eoi();
}
__attribute__((interrupt))
void general_protection_fault_handler(interrupt_frame_t* frame, uint64_t error_code) {
    uart_print("General Protection Fault");
    uart_print("\nRIP=");
    uart_print_hex(frame->rip);
    uart_print("\nError Code=");
    uart_print_hex(error_code);
    uart_print("\nProcess id=");
    uart_print_hex(now_process->process_id);
    uart_print("\n");
    while (1) {
        __asm__ __volatile__("hlt");
    }
}
__attribute__((interrupt))
void stack_segment_fault_handler(interrupt_frame_t* frame, uint64_t error_code) {
    uart_print("Stack Segment Fault");
    uart_print("\nRIP=");
    uart_print_hex(frame->rip);
    uart_print("\nError Code=");
    uart_print_hex(error_code);
    uart_print("\nProcess id=");
    uart_print_hex(now_process->process_id);
    uart_print("\n");
    while (1) {
        __asm__ __volatile__("hlt");
    }
}
__attribute__((naked))
void syscall_idthandler() {
    asm volatile(
        // 인터럽트 진입시 트랩 프레임이 이미 스택에 있음 (rip, cs, rflags, [rsp, ss])
        // 추가적으로 전역/일반 레지스터도 저장
        "push rax\n\t"
        "push rbx\n\t"
        "push rcx\n\t"
        "push rdx\n\t"
        "push rsi\n\t"
        "push rdi\n\t"
        "push rbp\n\t"
        "push r8\n\t"
        "push r9\n\t"
        "push r10\n\t"
        "push r11\n\t"
        "push r12\n\t"
        "push r13\n\t"
        "push r14\n\t"
        "push r15\n\t"
        "mov rax, ds\n\t"
        "push rax\n\t"
        "mov rax, es\n\t"
        "push rax\n\t"
        "mov rax, fs\n\t"
        "push rax\n\t"
        "mov rax, gs\n\t"
        "push rax\n\t"

        "mov rdi, rsp\n\t"
        "call syscall_handler\n\t"
        "pop rax\n\t"
        "mov gs, ax\n\t"
        "pop rax\n\t"
        "mov fs, ax\n\t"
        "pop rax\n\t"
        "mov es, ax\n\t"
        "pop rax\n\t"
        "mov ds, ax\n\t"
        "pop r15\n\t"
        "pop r14\n\t"
        "pop r13\n\t"
        "pop r12\n\t"
        "pop r11\n\t"
        "pop r10\n\t"
        "pop r9\n\t"
        "pop r8\n\t"
        "pop rbp\n\t"
        "pop rdi\n\t"
        "pop rsi\n\t"
        "pop rdx\n\t"
        "pop rcx\n\t"
        "pop rbx\n\t"
        "pop rax\n\t"

        "iretq\n\t"
        );
}
__attribute__((naked))
void waiting_idthandler() {
    asm volatile(
        // 인터럽트 진입시 트랩 프레임이 이미 스택에 있음 (rip, cs, rflags, [rsp, ss])
        // 추가적으로 전역/일반 레지스터도 저장
        "push rax\n\t"
        "push rbx\n\t"
        "push rcx\n\t"
        "push rdx\n\t"
        "push rsi\n\t"
        "push rdi\n\t"
        "push rbp\n\t"
        "push r8\n\t"
        "push r9\n\t"
        "push r10\n\t"
        "push r11\n\t"
        "push r12\n\t"
        "push r13\n\t"
        "push r14\n\t"
        "push r15\n\t"
        "mov rax, ds\n\t"
        "push rax\n\t"
        "mov rax, es\n\t"
        "push rax\n\t"
        "mov rax, fs\n\t"
        "push rax\n\t"
        "mov rax, gs\n\t"
        "push rax\n\t"

        "mov rdi, rsp\n\t"
        "call waiting_handler\n\t"
        "pop rax\n\t"
        "mov gs, ax\n\t"
        "pop rax\n\t"
        "mov fs, ax\n\t"
        "pop rax\n\t"
        "mov es, ax\n\t"
        "pop rax\n\t"
        "mov ds, ax\n\t"
        "pop r15\n\t"
        "pop r14\n\t"
        "pop r13\n\t"
        "pop r12\n\t"
        "pop r11\n\t"
        "pop r10\n\t"
        "pop r9\n\t"
        "pop r8\n\t"
        "pop rbp\n\t"
        "pop rdi\n\t"
        "pop rsi\n\t"
        "pop rdx\n\t"
        "pop rcx\n\t"
        "pop rbx\n\t"
        "pop rax\n\t"

        "iretq\n\t"
        );
}
extern "C" void waiting_handler(context_t* frame) {
    now_process->kernel_stack = (uint64_t*)frame;
    switch (frame->rax) {
	case 0x35:  //xHCI Event Interrupt
    {
		KEvent event;
		event.interval = 0;
		event.process_id = now_process->process_id;
		event.time = 0; // 타이머가 아니면 시간은 0으로 설정
		event.type = 0x35;
		event.arg[0] = frame->rdi; //
		event.arg[1] = frame->rsi; //
		event.arg[2] = frame->rdx; //
		xhci_event->push_back(event);
        break;
    }
    default:
		return; // 알 수 없는 인터럽트, 그냥 복귀
    }
    now_process->state |= 0b10; // 대기 상태
	now_process = next_process();
    uint64_t nowtime = tsc_get();
    next_process_time = nowtime + ms_to_ticks(now_process->time_slice);
    uint64_t nexttime = next_process_time;
    if (time_event->isEmpty() == false) {
        nexttime = min(next_process_time, time_event->top().time);
    }
    tsc_deadline_set(nexttime);
	now_process->run_process();
}

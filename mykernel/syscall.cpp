#include "syscall.h"
#include "process.h"
#include "log.h"
#include "memory.h"
#define GOP_PIXEL_FORMAT_RGBR     0   // PixelRedGreenBlueReserved8BitPerColor
#define GOP_PIXEL_FORMAT_BGRR     1   // PixelBlueGreenRedReserved8BitPerColor
#define GOP_PIXEL_FORMAT_BITMASK  2   // PixelBitMask
#define GOP_PIXEL_FORMAT_BLT_ONLY 3   // PixelBltOnly
struct Ginfo {
	uint64_t width;
	uint64_t height;
	uint64_t pitch;
	uint64_t format;
};
__attribute__((noinline)) void syscall_handler(context_t* frame) {
	switch (frame->rax) {
	case 1: // write
	{
		if (frame->rdi == 1) { // write to uart
			const char* buf = (const char*)frame->rsi;
			uint64_t len = frame->rdx;
			for (uint64_t i = 0; i < len; i++) {
				uart_putc(buf[i]);
			}
			frame->rax = len; // 반환값: 쓴 바이트 수
		}
		else {
			frame->rax = -1; // 반환값: 오류
		}
		break;
	}
	case 2: // read
	{
		// 아직 구현 안됨
		frame->rax = -1; // 반환값: 오류
		break;
	}
	case 3: // getpid
	{
		frame->rax = now_process->process_id; // 반환값: 현재 프로세스 ID
		break;
	}
	case 4: // message
	{
		if (frame->rdi == 0) { // send
			const char* msg = (const char*)frame->rsi;
			uint64_t pid = frame->rdx;
			Process* target_process = (Process*)(PROCESS_QUEUE_BASE + (sizeof(Process) * pid));
			if(target_process->state != 1) {
				frame->rax = -1; // 반환값: 오류 (존재하지 않는 프로세스)
				break;
			}
			target_process->msg_recv(msg, 0);
			frame->rax = 0; // 반환값: 성공
		}
		else if(frame->rdi == 1) { // pop
			char* out_msg = (char*)frame->rsi;
			uint64_t out_flags = 0;
			if (now_process->msg_pop(out_msg, out_flags)) {
				frame->rdx = out_flags;
				frame->rax = 0; // 반환값: 성공
			}
			else {
				frame->rax = -1; // 반환값: 오류
			}
		}
		else {
			frame->rax = -1; // 반환값: 오류
		}
	}
	case 5: // Graphics
	{
		if(frame->rdi == 0) { // Get Framebuffer Info
			Ginfo* ginfo = (Ginfo*)frame->rsi;
			ginfo->width = bootinfo->framebufferWidth;
			ginfo->height = bootinfo->framebufferHeight;
			ginfo->pitch = bootinfo->framebufferPitch;
			ginfo->format = bootinfo->framebufferFormat;
			frame->rax = 0; // 반환값: 성공
		}
		else if (frame->rdi == 2) { // Draw frame
			uint64_t bytesPerPixel;

			switch (bootinfo->framebufferFormat) { // Updated to use bootinfo->.format instead of ModeInfo->PixelFormat
			case GOP_PIXEL_FORMAT_RGBR:
			case GOP_PIXEL_FORMAT_BGRR:
				bytesPerPixel = 4;
				break;

			case GOP_PIXEL_FORMAT_BITMASK: {
				uint32_t mask = bootinfo->framebufferFormat;
				mask = bootinfo->framebufferWidth | bootinfo->framebufferHeight;
				uint32_t highest = 31;
				while (highest && ((mask >> highest) & 1) == 0)
					highest--;
				bytesPerPixel = ((highest + 1) + 7) / 8;
				break;
			}

			case GOP_PIXEL_FORMAT_BLT_ONLY:
			default:
				bytesPerPixel = 0;
				break;
			}
			uint64_t frame_size = bootinfo->framebufferHeight * bootinfo->framebufferPitch * bytesPerPixel;
			memcpy((void*)bootinfo->framebufferAddr, (void*)frame->rsi, frame_size);
			frame->rax = 0; // 반환값: 성공
		}
		else {
			frame->rax = -1; // 반환값: 오류
		}
		break;
	}
	case 9: // mmap
	{
		uint64_t size = frame->rdi;
		uint64_t addr = now_process->mmap(size, 0);
		frame->rax = addr; // 반환값: 매핑된 가상 주소
		uart_print("mmap size: ");
		uart_print(size);
		uart_print(", addr: ");
		uart_print_hex(addr);
		uart_print("\n");
		break;
	}
	case 45: // brk
	{
		uint64_t new_heap_bottom = frame->rdi;
		if (new_heap_bottom < now_process->heap_top) {
			frame->rax = now_process->heap_bottom; // 반환값: 현재 힙 탑
			uart_print("brk failed, heap_bottom: ");
			uart_print_hex(now_process->heap_bottom);
			uart_print(", new_heap_bottom: ");
			uart_print_hex(new_heap_bottom);
			uart_print("\n");
			break;
		}
		now_process->heap_bottom = new_heap_bottom;
		frame->rax = now_process->heap_bottom; // 반환값: 새로운 힙 바닥
		uart_print("brk new_heap_bottom: ");
		uart_print_hex(new_heap_bottom);
		uart_print("\n");
		break;
	}
	default:
		frame->rax = -1; // 반환값: 알 수 없는 시스템 콜
		break;
	}
}
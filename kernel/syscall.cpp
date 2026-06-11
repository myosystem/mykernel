#include "kernel/syscall.h"
#include "kernel/process.h"
#include "kernel/power.h"
#include "debug/log.h"
#include "util/memory.h"
#include "kernel/kernel.h"
#include "mm/shm.h"
#include "arch/lapic.h"
#include "arch/handler.h"
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
struct user_shm_request {
	uint64_t id;
	uint64_t size;
};
__attribute__((noinline)) void syscall_handler(context_t* frame) {
	switch (frame->rax) {
	case 1: // write
	{
		void*& slot = now_process->open_files[frame->rdi];
		File* file = (File*)slot;
		if (file != nullptr) { // write to uart
			const char* buf = (const char*)frame->rsi;
			uint64_t len = frame->rdx;
			file->write(buf, len);
			frame->rax = len; // 반환값: 쓴 바이트 수
		}
		else {
			frame->rax = -1; // 반환값: 오류
		}
		break;
	}
	case 2: // read
	{
		void*& slot = now_process->open_files[frame->rdi];
		File* file = (File*)slot;
		if (file != nullptr) { // write to uart
			char* buf = (char*)frame->rsi;
			uint64_t len = frame->rdx;
			file->read(buf, len);
			frame->rax = len; // 반환값: 읽은 바이트 수
		}
		else {
			frame->rax = -1; // 반환값: 오류
		}
		break;
	}
	case 8: // open
	{
		const char* path = (const char*)frame->rdi;
		File* file = vfs_open(path, now_process->current_partition, now_process->cwd_cluster);
		if (file != nullptr) {
			uart_print("Opened file: "); uart_print(path); uart_print("\n");
			uint64_t fd = now_process->open_files.push_back(file);
			frame->rax = fd; // 반환값: 파일 디스크립터 (인덱스)
		}
		else {
			uart_print("Failed to open "); uart_print(path); uart_print("\n");
			frame->rax = -1; // 반환값: 오류
		}
		break;
	}
	case 3: // getpid
	{
		frame->rax = now_process->id; // 반환값: 현재 프로세스 ID
		break;
	}
	case 4: // message
	{
		if (frame->rdi == 0) { // send
			msg_t* msg = (msg_t*)frame->rsi;
			msg->sender_pid = now_process->id; // 보낸이 PID 자동 설정
			msg->timestamp = tsc_get();
			uint64_t pid = frame->rdx;
			bool is_block = frame->rcx; // 메시지 대기 여부
			Process* target_process = GetProcess(pid);
			if (target_process == nullptr) {
				frame->rax = -1; // 반환값: 오류 (존재하지 않는 프로세스)
				break;
			}
			target_process->msg_recv(*msg, is_block);
			frame->rax = 0; // 반환값: 성공
		}
		else if(frame->rdi == 1) { // pop
			msg_t* out_msg = (msg_t*)frame->rsi;
			if (now_process->msg_pop(out_msg)) {
				frame->rax = 0; // 반환값: 성공
			}
			else {
				frame->rax = -1; // 반환값: 오류
			}
		}
		else if(frame->rdi == 2) { // empty
			frame->rax = now_process->msg_empty() ? 1 : 0; // 반환값: 메시지 큐가 비어있으면 1, 아니면 0
		}
		else if (frame->rdi == 0xFFFFFFFFFFFFFFFF) { // broadcast
			msg_t* msg = (msg_t*)frame->rsi;
			msg->sender_pid = now_process->id; // 보낸이 PID 자동 설정
			msg->timestamp = tsc_get();
			for(size_t i = 0; i < get_max_process_id(); i++) {
				Process* target_process = GetProcess(i);
				if(target_process && target_process->id != now_process->id) {
					target_process->msg_recv(*msg, false);
				}
			}
			frame->rax = 0; // 반환값: 성공
		}
		else {
			frame->rax = -1; // 반환값: 오류
		}
		break;
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
			break;
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
	case 6: // event
	{
		switch (frame->rdi) {
		case 0x0:
		{
			uint64_t event_id = frame->rsi;
			if (time_event->remove_by([event_id](const KEvent& e) { return e.event_id == event_id; })) {
				frame->rax = 0; // 반환값: 성공
			}
			else {
				frame->rax = -1; // 반환값: 오류 (존재하지 않는 이벤트)
			}
			break;
		}
		case 0x35: {
			static uint64_t timer_id_counter = 1;
			KEvent event;
			event.interval = ms_to_ticks(frame->rdx);
			event.process_id = now_process->id;
			event.time = tsc_get() + ms_to_ticks(frame->rsi); // 현재 시간 + 대기할 시간
			event.type = EVENT_TYPE_TIMER;
			event.event_id = timer_id_counter++;
			time_event->push(event);
			frame->rax = event.event_id;
			break;
		}
		default: {
			frame->rax = -1; // 반환값: 오류 (알 수 없는 이벤트 타입)
			break;
		}
		}
		break;
	}
	case 7: // get info
	{
		if (frame->rdi == 0) {
			frame->rax = tsc_get(); // 반환값: 현재 TSC 값
		}
		else if (frame->rdi == 1) {
			frame->rax = g_tsc_hz; // 반환값: TSC 주파수
		}
		else if (frame->rdi == 2) {
			frame->rax = phy_page_allocator->get_total_pages();
		}
		else if (frame->rdi == 3) {
			frame->rax = phy_page_allocator->get_used_pages();
		}
		else if (frame->rdi == 4) {
			frame->rax = phy_page_allocator->get_free_pages();
		}
		else if (frame->rdi == 5) {
			frame->rax = Process::get_count();
		}
		else if (frame->rdi == 6) {
			frame->rax = Process::max();
		}
		else {
			frame->rax = -1; // 반환값: 오류
		}
		break;
	}
	case 9: // mmap
	{
		uint64_t size = frame->rdi;
		uint64_t addr = now_process->mmap(size, MMAP_READ | MMAP_WRITE, 0);
		frame->rax = addr; // 반환값: 매핑된 가상 주소
		break;
	}
	case 10: // munmap
	{
		uint64_t addr = frame->rdi;
		uint64_t size = frame->rsi;
		if (now_process->munmap(addr, size)) {
			frame->rax = 0; // 반환값: 성공
			uart_print("munmap addr: ");
			uart_print_hex(addr);
			uart_print(", size: 0x");
			uart_print_hex(size);
			uart_print("\n");
		}
		else {
			frame->rax = -1; // 반환값: 오류
		}
		break;
	}
	case 15: // shared memory
	{
		uint64_t size = frame->rdi;
		user_shm_request* req = (user_shm_request*)frame->rsi;
		SharedMem* shm = new SharedMem(now_process->id, size);
		uint64_t id = shm->get_id();
		frame->rax = now_process->mmap(size, MMAP_READ | MMAP_WRITE | MMAP_SHARED, id); // 반환값: 매핑된 가상 주소
		req->id = id;
		req->size = size;
		uart_print("shared memory size: ");
		uart_print(size);
		uart_print(", id: ");
		uart_print_hex(id);
		uart_print("\n");
		break;
	}
	case 16: // accept shared memory
	{
		uint64_t id = frame->rdi;
		user_shm_request* req = (user_shm_request*)frame->rsi;
		SharedMem* shm = get_shared_mem(id);
		if (shm == nullptr) {
			frame->rax = -1; // 반환값: 오류 (존재하지 않는 공유 메모리)
			uart_print("accept shared memory failed, id: ");
			uart_print_hex(id);
			uart_print("\n");
			break;
		}
		frame->rax = now_process->mmap(shm->get_size(), MMAP_READ | MMAP_WRITE | MMAP_SHARED, id); // 반환값: 매핑된 가상 주소
		req->id = id;
		req->size = shm->get_size();
		uart_print("accept shared memory id: ");
		uart_print_hex(id);
		uart_print(", addr: ");
		uart_print_hex(frame->rax);
		uart_print("\n");
		break;
	}
	case 30: // fork
	{
		frame->rax = now_process->fork(frame);
		break;
	}
	case 31: // exec
	{
		const char* path = (const char*)frame->rdi;
		const char** argv = (const char**)frame->rsi;
		frame->rax = now_process->exec(path, argv, frame); // 반환값: 성공하면 0, 실패하면 -1
		break;
	}
	case 32: // wait
	{
		frame->rax = now_process->wait(); // 반환값: 종료된 자식 프로세스 ID, 오류 시 -1
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
	case 50: // exit
	{
		Process* exiting = now_process;
		now_process = next_process();
		exiting->time_slice = frame->rdi; // 리턴값 남는곳에 저장
		exiting->~Process();
		if (exiting->parent == (uint64_t)-1) {
			Process::operator delete(exiting);
		}
		now_process->run_process();
		break;
	}
	case -1ull:
	{
		shutdown();
	}
	case 62: // lseek
	{
		File* file = (File*)now_process->open_files[frame->rdi];
		if (!file) { frame->rax = (uint64_t)-1; break; }
		int64_t offset = (int64_t)frame->rsi;
		int whence = (int)frame->rdx;
		int64_t new_offset;
		if      (whence == 0) new_offset = offset;
		else if (whence == 1) new_offset = (int64_t)file->tell() + offset;
		else if (whence == 2) new_offset = (int64_t)file->size() + offset;
		else { frame->rax = (uint64_t)-1; break; }
		if (new_offset < 0) { frame->rax = (uint64_t)-1; break; }
		if (file->seek((uint64_t)new_offset) < 0) { frame->rax = (uint64_t)-1; break; }
		frame->rax = file->tell();
		break;
	}
	default:
		frame->rax = -1; // 반환값: 알 수 없는 시스템 콜
		break;
	}
}
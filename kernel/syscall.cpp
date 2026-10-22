#include "kernel/syscall.h"
#include "kernel/process.h"
#include "kernel/power.h"
#include "kernel/pipe.h"
#include "kernel/timer_handler.h"
#include "kernel/kernel.h"
#include "debug/log.h"
#include "util/memory.h"
#include "mm/shm.h"
#include "arch/lapic.h"
#include "arch/handler.h"
#include "filesys/file.h"
#include "net/socket.h"
#include "net/icmp.h"
#include "net/tcp.h"
#include "net/udp.h"
extern uint64_t get_cycles();
extern vector<NetDevice*>* netdevices;
#define min(a, b) ((a) < (b) ? (a) : (b))
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
			frame->rax = file->write(buf, len); // 반환값: 쓴 바이트 수
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
			frame->rax = file->read(buf, len); // 반환값: 읽은 바이트 수
		}
		else {
			frame->rax = -1; // 반환값: 오류
		}
		break;
	}
	case 8: // fd
	{
		if (frame->rdi == 0) { // open
			const char* path = (const char*)frame->rsi;
			uint64_t flags = frame->rdx;
			File* file = vfs_open(path, now_process->current_partition, now_process->cwd_cluster);
			if (file == nullptr && (flags & 0x40)) {
				file = vfs_create(path, now_process->current_partition, now_process->cwd_cluster);
			}
			if (file != nullptr && (flags & 0x200)) file->truncate();
			if (file != nullptr && (flags & 0x10000) && !file->is_directory()) { file->close(); file = nullptr; }
			if (file != nullptr) {
				uart_print("Opened file: "); uart_print(path); uart_print("\n");
				uint64_t fd = now_process->open_files.push_back(file);
				frame->rax = fd; // 반환값: 파일 디스크립터 (인덱스)
			}
			else {
				uart_print("Failed to open "); uart_print(path); uart_print("\n");
				frame->rax = -1; // 반환값: 오류
			}
		}
		else if (frame->rdi == 1) { // close
			uint64_t fd = frame->rsi;
			void*& slot = now_process->open_files[fd];
			File* file = (File*)slot;
			if (file != nullptr) {
				file->close();
				slot = nullptr;
				frame->rax = 0;
			}
			else {
				frame->rax = -1;
			}
		}
		else if (frame->rdi == 2) { // dup
			uint64_t old_fd = frame->rsi;
			uint64_t new_fd = frame->rdx;
			if (old_fd == new_fd) {
				frame->rax = 0;
				break;
			}
			if (new_fd + 1 > now_process->open_files.get_size()) {
				frame->rax = -1; // 지금 push_back방식으로 추가해서 이건 나중에 다 뜯어고치면서 구현
				break;
			}
			void*& old_slot = now_process->open_files[old_fd];
			File* file = (File*)old_slot;
			if (file != nullptr) {
				file->open();
				void*& new_slot = now_process->open_files[new_fd];
				file = (File*)new_slot;
				if (file) {
					file->close();
				}
				new_slot = old_slot;
				frame->rax = new_fd;
			}
			else {
				frame->rax = -1;
			}
		}
		else if (frame->rdi == 3) { // mkdir
			const char* mpath = (const char*)frame->rsi;
			frame->rax = vfs_mkdir(mpath, now_process->current_partition, now_process->cwd_cluster);
		}
		else if (frame->rdi == 4) { // unlink
			const char* upath = (const char*)frame->rsi;
			frame->rax = vfs_unlink(upath, now_process->current_partition, now_process->cwd_cluster);
		}
		else if (frame->rdi == 5) { // rmdir
			const char* rpath = (const char*)frame->rsi;
			frame->rax = vfs_rmdir(rpath, now_process->current_partition, now_process->cwd_cluster);
		}
		else if (frame->rdi == 6) { // poll_register (nonblock + notify self)
			uint64_t pfd = frame->rsi;
			void*& pslot = now_process->open_files[pfd];
			File* pf = (File*)pslot;
			if (pf) { pf->poll_register(now_process->id, frame->rdx); frame->rax = 0; }
			else frame->rax = -1;
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
		else if (frame->rdi == 1) { // pop
			msg_t* out_msg = (msg_t*)frame->rsi;
			if (now_process->msg_pop(out_msg)) {
				frame->rax = 0; // 반환값: 성공
			}
			else {
				frame->rax = -1; // 반환값: 오류
			}
		}
		else if (frame->rdi == 2) { // empty
			frame->rax = now_process->msg_empty() ? 1 : 0; // 반환값: 메시지 큐가 비어있으면 1, 아니면 0
		}
		else if (frame->rdi == 0xFFFFFFFFFFFFFFFF) { // broadcast
			msg_t* msg = (msg_t*)frame->rsi;
			msg->sender_pid = now_process->id; // 보낸이 PID 자동 설정
			msg->timestamp = tsc_get();
			for (size_t i = 0; i < get_max_process_id(); i++) {
				Process* target_process = GetProcess(i);
				if (target_process && target_process->id != now_process->id) {
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
		if (frame->rdi == 0) { // Get Framebuffer Info
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
		else if (frame->rdi == 7) {
			frame->rax = get_cycles(); // guest exec cycles (PMU; tsc fallback)
		}
		else if (frame->rdi >= 8 && frame->rdi < 16) {
#ifdef TEST_MODE
			extern uint64_t forkdbg[8];
			frame->rax = forkdbg[frame->rdi - 8];
#else
			frame->rax = 0;
#endif
		}
		else if (frame->rdi >= 16 && frame->rdi < 32) {
#ifdef TEST_MODE
			extern uint64_t cowlog[16];
			frame->rax = cowlog[frame->rdi - 16];
#else
			frame->rax = 0;
#endif
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
	case 29: // console
	{
		if (frame->rdi == 0) { // set
			uint64_t pid = frame->rsi;
			Process* target_process = GetProcess(pid);
			if (target_process == nullptr) {
				frame->rax = -1; // 반환값: 오류 (존재하지 않는 프로세스)
				break;
			}
			now_process->console_pid = pid;
			frame->rax = 0; // 반환값: 성공
		}
		else if (frame->rdi == 1) { // get
			frame->rax = now_process->console_pid; // 반환값: 콘솔 프로세스 ID
		}
		else {
			frame->rax = -1; // 반환값: 오류 (알 수 없는 콘솔 명령)
		}
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
	case 33: // pipe
	{
		int* result = (int*)frame->rdi;
		Pipe* pipe = new Pipe();
		File* in = pipe->pipe_in();
		File* out = pipe->pipe_out();
		result[0] = (int)now_process->open_files.push_back(in);
		result[1] = (int)now_process->open_files.push_back(out);
		frame->rax = 0;
		break;
	}
	case 34: // yield
	{
		now_process->save_sse();
		process_queue->enqueue(now_process->id);
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
	case 36: // socket
	{
		if (frame->rdi == 0) { // create socket
			Socket* socket = nullptr;
			if (frame->rsi == 1) {
				socket = ICMPSocket::Create();
			}
			else if (frame->rsi == 2) {
				socket = TCPSocket::Create(frame->rdx);
			}
			else if (frame->rsi == 3) {
				socket = UDPSocket::Create(frame->rdx);
			}
			frame->rax = socket != nullptr ? socket->id : (uint64_t)-1;
		}
		else if (frame->rdi == 1) { // send  (type=r8, seq=r9, data=r10, ttl=r12, len=r13)
			Socket* socket = (Socket*)Socket::get((uint16_t)frame->rsi);
			if (socket) {
				string data((uint8_t*)frame->r10, (uint16_t)frame->r13);
				socket->send(data);
				frame->rax = 0; // 반환값: 성공
			}
			else {
				frame->rax = -1; // 반환값: 오류 (존재하지 않는 소켓)
			}
		}
		else if (frame->rdi == 2) { // recv  (type*=rdx, seq*=r8, data=r9, len*=r10)
			Socket* socket = (Socket*)Socket::get((uint16_t)frame->rsi);
			if (socket) {
				uint8_t*  utype = (uint8_t*)frame->rdx;
				uint16_t* useq  = (uint16_t*)frame->r8;
				uint8_t*  udata = (uint8_t*)frame->r9;
				uint16_t* ulen  = (uint16_t*)frame->r10;
				size_t len = *ulen;
				RxMsg data;
				uint64_t result = socket->recv(data, len);
				if (result == -1) {
					frame->rax = -1;
					break;
				}
				data.data.copy_out(0, udata, data.data.size() > *ulen ? *ulen : data.data.size());
				*ulen = data.data.size();
				frame->rax = result; // 반환값: 데이터 길이
			}
			else {
				frame->rax = -1; // 반환값: 오류 (존재하지 않는 소켓)
			}
		}
		else if (frame->rdi == 3) { // connect
			Socket* socket = (Socket*)Socket::get((uint16_t)frame->rsi);
			if (socket) { socket->connect(frame->rdx, frame->rbx); frame->rax = 0; }   // rdx = dst_ip, rbx = dst_port
			else frame->rax = -1;
		}
		else if (frame->rdi == 4) { // set device
			Socket* socket = (Socket*)Socket::get((uint16_t)frame->rsi);
			if (socket) { socket->set_device(frame->rdx); frame->rax = 0; }   // rdx = device_id
			else frame->rax = -1;
		}
		else if (frame->rdi == 5) { // set flags
			Socket* socket = (Socket*)Socket::get((uint16_t)frame->rsi);
			if (!socket) { frame->rax = -1; break; }
			uint64_t blocking = !!(frame->rdx & SOCKET_FLAG_BLOCKING);
			uint64_t message = !!(frame->rdx & SOCKET_FLAG_MESSAGE);
			socket->state = (blocking << 1) | (message << 2) | 1;
			if (message)
				socket->msg_pid = now_process->id;
			else
				socket->msg_pid = (uint64_t)-1;
			frame->rax = 0;
		}
		else {
			frame->rax = -1; // 반환값: 오류 (알 수 없는 소켓 명령)
		}
		break;
	}
	case 37: // netdevice
	{
		if (frame->rdi == 0) { //get count
			frame->rax = netdevices[0].size();
		}
		if (frame->rdi == 1) { //get
			frame->rax = netdevices[0][frame->rsi]->src_ip();
		}
		if (frame->rdi == 2) { //set
			netdevices[0][frame->rsi]->set_ip(frame->rdx);
		}
		if (frame->rdi == 3) {
			char* mac_buf = (char*)frame->rdx;
			netdevices[0][frame->rsi]->get_mac(mac_buf);
			frame->rax = 0;
		}
		break;
	}
	case 38: // route table
	{
		if (frame->rdi == 0) {
			RouteTable::add(frame->r8, frame->r9, frame->r10, netdevices[0][frame->rsi]);
		}
		if (frame->rdi == 1) {
			RouteTable::del(netdevices[0][frame->rsi]);
		}
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
		exiting->time_slice = frame->rdi; // 리턴값 남는곳에 저장
		exiting->~Process();
		if (exiting->parent == (uint64_t)-1) {
			Process::operator delete(exiting);
		}
		now_process = next_process();
		now_process->run_process();
		break;
	}
	case -1ull: // power
	{
		if (frame->rdi == 0)
			shutdown();
		if (frame->rdi == 1)
			reboot();
		frame->rax = -1; // 실패
		break;
	}
	case 62: // lseek
	{
		File* file = (File*)now_process->open_files[frame->rdi];
		if (!file) { frame->rax = (uint64_t)-1; break; }
		int64_t offset = (int64_t)frame->rsi;
		int whence = (int)frame->rdx;
		int64_t new_offset;
		if (whence == 0) new_offset = offset;
		else if (whence == 1) new_offset = (int64_t)file->tell() + offset;
		else if (whence == 2) new_offset = (int64_t)file->size() + offset;
		else { frame->rax = (uint64_t)-1; break; }
		if (new_offset < 0) { frame->rax = (uint64_t)-1; break; }
		if (file->seek((uint64_t)new_offset) < 0) { frame->rax = (uint64_t)-1; break; }
		frame->rax = file->tell();
		break;
	}
	case 35: // chdir
	{
		const char* path = (const char*)frame->rdi;
		Partition* new_part = nullptr;
		uint64_t   new_cluster = 0;
		int ret = vfs_chdir(path, now_process->current_partition, now_process->cwd_cluster,
			&new_part, &new_cluster);
		if (ret == 0) {
			now_process->current_partition = new_part;
			now_process->cwd_cluster = new_cluster;
		}
		frame->rax = ret;
		break;
	}
	default:
		frame->rax = -1; // 반환값: 알 수 없는 시스템 콜
		break;
	}
}
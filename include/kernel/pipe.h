#ifndef __PIPE_H__
#define __PIPE_H__
#include "filesys/file.h"
#include "kernel/process.h"
#include "util/queue.h"
#include "util/util.h"
#define PIPE_NONBLOCK 0x2
#define PIPE_NOTIFY 0x4
#define MSG_PIPE_DATA 0x1001
class Pipe;
class InPipe : public File{
private:
	Pipe* pipe;
public:
	InPipe(Pipe* pipe) : File(nullptr,0,0,0), pipe(pipe) {}
    int read(void* buf, uint32_t len) override;
	int write(const void* buf, uint32_t len) override {
		return -1;
	}
	void poll_register(uint64_t pid, uint64_t opts) override;
    void close();
	friend Pipe;
};
class OutPipe : public File {
private:
	Pipe* pipe;
public:
	OutPipe(Pipe* pipe) : File(nullptr,0,0,0), pipe(pipe) {}
	int read(void* buf, uint32_t len) override {
		return -1;
	}
    int write(const void* buf, uint32_t len) override;
    void poll_register(uint64_t pid, uint64_t opts) override;
    void close();
	friend Pipe;
};
class Pipe : public File{
private:
	InPipe* in;
	OutPipe* out;
	uint8_t* buf;
    uint64_t read_ptr = 0;
    uint64_t write_ptr = 0;
	queue<uint64_t> in_blocking;
	queue<uint64_t> out_blocking;
public:
    Pipe() : File(nullptr, 0, 0, 0), in(new InPipe(this)), out(new OutPipe(this)) {
        File::open();
        buf = (uint8_t*)(phy_page_allocator->alloc_phy_page() + HHDM_BASE);
    }
    ~Pipe() {
        phy_page_allocator->put_page((uint64_t)buf - HHDM_BASE);
    }

    File* pipe_in()  const { return in; }
    File* pipe_out() const { return out; }

    int read(void* dst, uint32_t len) override {
        uint64_t to_read = len;
        while (to_read > 0) {
            uint64_t rp = read_ptr;
            uint64_t wp = write_ptr;
            uint64_t can_read = (wp - rp + PageSize) % PageSize;

            if (can_read == 0) {
                if (out == nullptr) return len - to_read; // EOF
                if (in && (in->state & PIPE_NONBLOCK)) return (to_read == len) ? -1 : (int)(len - to_read);
                in_blocking.enqueue(now_process->id);
                simple_wait();
                continue;
            }

            uint64_t now_read = can_read < to_read ? can_read : to_read;
            uint64_t dst_off = len - to_read;
            uint64_t to_end = PageSize - rp;

            if (now_read <= to_end) {
                memcpy((uint8_t*)dst + dst_off, buf + rp, now_read);
            }
            else {
                memcpy((uint8_t*)dst + dst_off, buf + rp, to_end);
                memcpy((uint8_t*)dst + dst_off + to_end, buf, now_read - to_end);
            }

            read_ptr = (rp + now_read) % PageSize;
            to_read -= now_read;

            while (out_blocking.get_size() > 0)
                add_process(out_blocking.dequeue());
        }
        return len;
    }

    int write(const void* src, uint32_t len) override {
        bool was_empty = (read_ptr == write_ptr);
        uint64_t to_write = len;
        while (to_write > 0) {
            uint64_t rp = read_ptr;
            uint64_t wp = write_ptr;
            uint64_t can_write = (rp - wp - 1 + PageSize) % PageSize;

            if (can_write == 0) {
                if (in == nullptr) return -1; // reader 없음 (SIGPIPE 자리)
                if (out && (out->state & PIPE_NONBLOCK)) return (to_write == len) ? -1 : (int)(len - to_write);
                out_blocking.enqueue(now_process->id);
                simple_wait();
                continue;
            }

            uint64_t now_write = can_write < to_write ? can_write : to_write;
            uint64_t src_off = len - to_write;
            uint64_t to_end = PageSize - wp;

            if (now_write <= to_end) {
                memcpy(buf + wp, (const uint8_t*)src + src_off, now_write);
            }
            else {
                memcpy(buf + wp, (const uint8_t*)src + src_off, to_end);
                memcpy(buf, (const uint8_t*)src + src_off + to_end, now_write - to_end);
            }

            write_ptr = (wp + now_write) % PageSize;
            to_write -= now_write;

            if (in && !(in->state & PIPE_NONBLOCK)) while (in_blocking.get_size() > 0)
                add_process(in_blocking.dequeue());
        }
        if (was_empty && in && (in->state & PIPE_NOTIFY) && in_blocking.get_size() > 0) { Process* np = GetProcess(*in_blocking.peek_back()); if (np) { msg_t m = {}; m.type = MSG_PIPE_DATA; np->msg_recv(m, false); } }
        return len;
    }
	friend InPipe;
	friend OutPipe;
};
#endif // __PIPE_H__
#include "kernel/pipe.h"

int InPipe::read(void* buf, uint32_t len) {
	return pipe->read(buf, len);
}
void InPipe::close() {
	if (get_refcount() == 1) {
		pipe->in = nullptr;
		while (pipe->out_blocking.get_size() > 0)
			add_process(pipe->out_blocking.dequeue());
		pipe->close();
	}
	File::close();
}
int OutPipe::write(const void* buf, uint32_t len) {
	return pipe->write(buf, len);
}
void OutPipe::close() {
	if (get_refcount() == 1) {
		pipe->out = nullptr;
		while (pipe->in_blocking.get_size() > 0)
			add_process(pipe->in_blocking.dequeue());
		pipe->close();
	}
	File::close();
}

void InPipe::poll_register(uint64_t pid, uint64_t opts) {
	if (opts & 1) this->state |= PIPE_NONBLOCK;
	if (opts & 2) { this->state |= PIPE_NOTIFY; pipe->in_blocking.enqueue(pid); }
}

void OutPipe::poll_register(uint64_t pid, uint64_t opts) {
	if (opts & 1) this->state |= PIPE_NONBLOCK;
}

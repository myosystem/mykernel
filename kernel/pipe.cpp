#include "kernel/pipe.h"

int InPipe::read(void* buf, uint32_t len) {
	return pipe->read(buf, len);
}
void InPipe::close() {
	if (get_refcount() == 1) {
		pipe->in = nullptr;
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
		pipe->close();
	}
	File::close();
}
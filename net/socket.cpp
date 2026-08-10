#include "net/socket.h"

void Socket::recv(RxMsg& data) {
	while (recv_queue.isEmpty()) {
		wait_queue.enqueue(now_process->id);
		now_process->state |= PROCESS_STATE_SOCKET_WAIT;
		simple_wait();
		now_process->state = now_process->state & ~PROCESS_STATE_SOCKET_WAIT;
	}
	RxMsg m = recv_queue.dequeue();
	data = (RxMsg&&)m;
	return;
}
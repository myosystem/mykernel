#include "net/socket.h"

uint64_t Socket::recv(RxMsg& data) {
	if (state & SOCKET_FLAG_BLOCKING) {
		while (recv_queue.isEmpty()) {
			wait_queue.enqueue(now_process->id);
			now_process->state |= PROCESS_STATE_SOCKET_WAIT;
			simple_wait();
			now_process->state = now_process->state & ~PROCESS_STATE_SOCKET_WAIT;
		}
	}
	else {
		if (recv_queue.isEmpty()) {
			return -1;
		}
	}
	RxMsg m = recv_queue.dequeue();
	data = (RxMsg&&)m;
	return 0;
}
extern vector<NetDevice*>* netdevices;
bool Socket::set_device(uint64_t device_id) {
	if (device_id == (uint64_t)-1) { dev_bound = false; return true; }
	if (device_id >= netdevices->size()) return false;
	route.dev = (*netdevices)[device_id];
	route.gateway = 0;
	route.dest = 0;
	route.netmask = 0;
	dev_bound = true;
	return true;
}
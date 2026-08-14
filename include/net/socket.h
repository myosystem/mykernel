#ifndef __SOCKET_H__
#define __SOCKET_H__
#include "util/new.h"
#include "util/queue.h"
#include "util/string.h"
#include "kernel/kernel.h"
#include "kernel/process.h"
#include "net/netdevice.h"
#include "kernel/syscall.h"
#define SOCKET_FLAG_BLOCKING 0b10
#define SOCKET_FLAG_MESSAGE 0b100
struct RxMsg {                 // 수신 메시지 (계층 파싱 결과)
	uint32_t src_ip;
	uint32_t dst_ip;
	uint32_t src_port;
	uint32_t dst_port;
	string   data;
};
class Socket : public NewObject<pml4_addr(PML4::SOCKET_HEAP),0x200,nullptr,nullptr> {
protected:
	queue<RxMsg> recv_queue;
	queue<uint64_t> wait_queue;
	uint32_t dst_ip_;
	uint16_t dst_port_;
	uint8_t ttl = 64;
	Route route;
	bool dev_bound;
public:
	Socket()
		:dst_ip_(0), dev_bound(false) {
		state |= SOCKET_FLAG_BLOCKING;
	}
	virtual ~Socket() {}
	virtual bool connect(uint32_t dst_ip, uint16_t dst_port) {
		if (!dev_bound && !RouteTable::find(dst_ip, &route)) return false;
		dst_ip_ = dst_ip;
		dst_port_ = dst_port;
		return true;
	}
	bool set_device(uint64_t device_id);
	void send(string data) { sendto(dst_ip_, dst_port_, data, &route); }
	virtual void sendto(uint32_t dst_ip, uint16_t dst_port, string& data, Route* route = nullptr) = 0;
	virtual uint64_t recv(RxMsg& data);
	uint64_t msg_pid;
};
#endif // __SOCKET_H__
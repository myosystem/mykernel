#ifndef __NETDEVICE_H__
#define __NETDEVICE_H__

#include "kernel/kernel.h"
#include "util/controller.h"
#include "util/queue.h"
#include "util/memory.h"
#include "util/page.h"
#include "util/new.h"
#include "net/ip.h"


class NetDevice {
protected:
	uint32_t src_ip_;
public:
	NetDevice()
		: src_ip_(0) {}
	~NetDevice() {}
	virtual void init() = 0;
	virtual void send(string& packet, uint32_t next_hop, uint16_t ethertype) = 0;   // next_hop=ARP대상, ethertype=상위L3(0x0800 IPv4 등)
	virtual uint64_t max_packet_size() = 0;
	uint32_t src_ip() { return src_ip_; }
	void set_ip(uint32_t src_ip) {
		src_ip_ = src_ip;
	}
	virtual void get_mac(char* mac_buf) {
		memset(mac_buf, 0, 6);
	}
};
struct Route {
	uint32_t dest;      // 목적지 네트워크 (예: 127.0.0.0)
	uint32_t netmask;   // 넷마스크 (예: 0xFF000000 = /8)
	uint32_t gateway;   // next-hop (0 = 직접연결/on-link)
	NetDevice* dev;     // 어느 인터페이스로
};

namespace RouteTable {
	void init();
	void add(uint32_t dest, uint32_t netmask, uint32_t gateway, NetDevice* dev);
	void del(NetDevice* dev);
	bool find(uint32_t ip, Route* route_out);
}
class LoopBack : public NetDevice {
protected:
	struct QPacket { Page data; uint64_t len; };
public:
	LoopBack()
		: NetDevice() {}
	void init() override {}
	void send(string& packet, uint32_t next_hop, uint16_t ethertype) override;
	uint64_t max_packet_size() override { return 4096; }
};
#endif // __NETDEVICE_H__
#ifndef __ETHERNET_H__
#define __ETHERNET_H__

#include "net/netdevice.h"
#include "util/vector.h"
#include "util/queue.h"
#include "kernel/process.h"
#define ETH_TYPE_ARP 0x0806
#define ETH_TYPE_IP 0x0800
struct eth_header {
	uint8_t  dst[6];
	uint8_t  src[6];
	uint16_t ethertype;   // big-endian
} __attribute__((packed));
struct arp_cache {
	uint32_t ip;
	uint8_t mac[6];
	uint64_t timestamp;   // last update time
};
struct arp_packet {
	uint16_t htype;   // 하드웨어 타입: 1 = Ethernet
	uint16_t ptype;   // 프로토콜 타입: 0x0800 = IPv4
	uint8_t  hlen;    // 하드웨어 주소 길이: 6 (MAC)
	uint8_t  plen;    // 프로토콜 주소 길이: 4 (IPv4)
	uint16_t oper;    // 1 = request, 2 = reply
	uint8_t  sha[6];  // sender MAC
	uint32_t spa;     // sender IP
	uint8_t  tha[6];  // target MAC (request 땐 0)
	uint32_t tpa;     // target IP
} __attribute__((packed));
class Ethernet : public NetDevice {
private:
	void insert_arp(uint32_t ip, uint8_t* dst_mac);
protected:
	uint8_t src_mac[6];
	vector<arp_cache> arp_table;
	queue<uint64_t> arp_wait_queue;
	uint8_t* find_mac(uint32_t ip);
	void add_arp(uint32_t ip);
public:
	Ethernet()
		: NetDevice(), src_mac() {
		
	}
	void init() override = 0;
	void send(string& packet, uint32_t dst_ip, uint16_t ethertype) override;
	void recv(string& packet);
	virtual void hw_send(string& packet) = 0;
	uint64_t max_packet_size() override { return 1500; }
	void get_mac(char* mac_buf) override { memcpy(mac_buf, src_mac, 6); }
};
#endif // __ETHERNET_H__
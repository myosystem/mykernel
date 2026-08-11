#include "net/ethernet.h"
#include "arch/lapic.h"
void Ethernet::insert_arp(uint32_t ip, uint8_t* dst_mac) {
	for (int i = 0; i < arp_table.size(); i++) {
		if (arp_table[i].ip == ip) {
			memcpy(arp_table[i].mac, dst_mac, 6);
			if (!arp_wait_queue.isEmpty()) add_process(arp_wait_queue.dequeue());
			return;
		}
	}
	arp_cache new_entry;
	new_entry.ip = ip;
	memcpy(new_entry.mac, dst_mac, 6);
	new_entry.timestamp = tsc_get();
	arp_table.push_back(new_entry);
	if (!arp_wait_queue.isEmpty()) add_process(arp_wait_queue.dequeue());
}
uint8_t* Ethernet::find_mac(uint32_t ip) {
	while (1) {
		for (int i = 0; i < arp_table.size(); i++) {
			if (arp_table[i].ip == ip) {
				return arp_table[i].mac;
			}
		}
		add_arp(ip);   // ARP 요청 보내기
	}
}
void Ethernet::add_arp(uint32_t ip) {
	arp_packet arp = {};
	arp.htype = swap16(1);   // Ethernet
	arp.ptype = swap16(0x0800);   // IPv4
	arp.hlen = 6; arp.plen = 4;
	arp.oper = swap16(1);   // request
	memcpy(arp.sha, src_mac, 6);
	arp.spa = swap32(src_ip());
	memset(arp.tha, 0, 6);
	arp.tpa = swap32(ip);
	string packet((uint8_t*)&arp, sizeof(arp_packet));
	send(packet, -1, ETH_TYPE_ARP);
	arp_wait_queue.enqueue(now_process->id);
	simple_wait();
}
void Ethernet::send(string& packet, uint32_t dst_ip, uint16_t ethertype) {
	eth_header eth;
	memcpy(eth.src, src_mac, 6);
	if (dst_ip == -1)
		memset(eth.dst, -1, 6);
	else
		memcpy(eth.dst, find_mac(dst_ip), 6);
	eth.ethertype = swap16(ethertype);
	packet.prepend((uint8_t*)&eth, sizeof(eth_header));
	hw_send(packet);
}
void Ethernet::recv(string& packet) {
	if (packet.size() < sizeof(eth_header)) return;
	eth_header eth;
	packet.copy_out(0, (uint8_t*)&eth, sizeof(eth_header));
	uint16_t etype = swap16(eth.ethertype);
	packet = packet.substr(sizeof(eth_header), packet.size() - sizeof(eth_header));
	if (etype == ETH_TYPE_IP) {
		IPv4::recv(packet);
	}
	else if (etype == ETH_TYPE_ARP) {
		arp_packet arp;
		packet.copy_out(0, (uint8_t*)&arp, sizeof(arp_packet));
		if (swap16(arp.oper) == 1) {   // request
			if (swap32(arp.tpa) == src_ip()) {
				insert_arp(swap32(arp.spa), arp.sha);

				arp_packet reply = {};
				reply.htype = swap16(1);   // Ethernet
				reply.ptype = swap16(0x0800);   // IPv4
				reply.hlen = 6; reply.plen = 4;
				reply.oper = swap16(2);   // reply
				memcpy(reply.sha, src_mac, 6);
				reply.spa = swap32(src_ip());
				memcpy(reply.tha, arp.sha, 6);
				reply.tpa = arp.spa;
				string reply_packet((uint8_t*)&reply, sizeof(arp_packet));
				send(reply_packet, swap32(arp.spa), ETH_TYPE_ARP);
			}
		}
		else if (swap16(arp.oper) == 2) {   // reply
			insert_arp(swap32(arp.spa), arp.sha);
		}
	}
}
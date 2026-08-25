#include "net/ip.h"
#include "net/icmp.h"
#include "net/udp.h"
#include "net/tcp.h"
#include "net/ethernet.h"
#include "net/netdevice.h"
uint32_t ipaddr(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
	return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d;
}
uint16_t ip_checksum(const void* data, int len) {
    const uint16_t* p = (const uint16_t*)data;
    uint32_t sum = 0;
    while (len > 1) { sum += *p++; len -= 2; }           // 16비트씩 다 더함
    if (len == 1) sum += *(const uint8_t*)p;             // 홀수 바이트 처리
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16); // 캐리 접기(fold)
    return (uint16_t)~sum;                                // 1의 보수
}
static uint16_t ip_id = 1;
uint16_t ip_next_id() {
	if (ip_id == 0) ip_id = 1;
    return ip_id++; 
}
namespace IPv4 {
	void send(Route* route, uint32_t dst, uint8_t proto, string& payload, uint8_t ttl) {
		uint32_t next_hop = route->gateway ? route->gateway : dst;   // off-link면 게이트웨이, on-link면 dst
		if (!route->dev) return;
		uint64_t max_packet_size = route->dev->max_packet_size();
		if (payload.size() + sizeof(ipv4_header) > max_packet_size) {
			ipv4_header header;
			header.dst_ip = swap32(dst);
			header.src_ip = swap32(route->dev->src_ip());
			header.flags_frag = 0;
			header.id = swap16(ip_next_id());
			header.protocol = proto;
			header.tos = 0;
			header.ttl = ttl;
			header.version_ihl = (IPV4_VERSION << 4) | (sizeof(ipv4_header) / 4);
			header.total_length = swap16(sizeof(ipv4_header) + payload.size());
			header.checksum = 0;
			header.checksum = ip_checksum(&header, sizeof(ipv4_header));
			uint64_t maxdata = (max_packet_size - sizeof(ipv4_header)) & ~7ULL;   // 8의 배수로 내림 (fragment offset 단위)
			for (uint64_t offset = 0; offset < payload.size(); offset += maxdata) {
				uint64_t chunk_size = (payload.size() - offset > maxdata) ? maxdata : payload.size() - offset;
				header.flags_frag = swap16((offset / 8) & 0x1FFF);
				if (offset + chunk_size < payload.size()) header.flags_frag |= swap16(IPV4_FRAG_MF);
				header.total_length = swap16(sizeof(ipv4_header) + chunk_size);
				header.checksum = 0;
				header.checksum = ip_checksum(&header, sizeof(ipv4_header));
				string packet;
				packet.append_from(payload, offset, chunk_size);            // 조각 데이터
				packet.prepend((uint8_t*)&header, sizeof(ipv4_header));      // IP 헤더 앞에 (복사 0)
				route->dev->send(packet, next_hop, ETH_TYPE_IP);
			}
		}
		else {
			ipv4_header header;
			header.dst_ip = swap32(dst);
			header.src_ip = swap32(route->dev->src_ip());
			header.flags_frag = 0;
			header.id = swap16(ip_next_id());
			header.protocol = proto;
			header.tos = 0;
			header.ttl = ttl;
			header.version_ihl = (IPV4_VERSION << 4) | (sizeof(ipv4_header) / 4);
			header.total_length = swap16(sizeof(ipv4_header) + payload.size());
			header.checksum = 0;
			header.checksum = ip_checksum(&header, sizeof(ipv4_header));
			payload.prepend((uint8_t*)&header, sizeof(ipv4_header));     // IP 헤더 앞에 (payload 복사 0)
			route->dev->send(payload, next_hop, ETH_TYPE_IP);
		}
	}
	void recv(string& packet) {
		if (packet.size() < sizeof(ipv4_header)) return;
		ipv4_header ip;
		packet.copy_out(0, (uint8_t*)&ip, sizeof(ipv4_header));
		uint32_t ihl = (ip.version_ihl & 0xF) * 4;
		if (ip_checksum(&ip, ihl) != 0) return;               // IP 헤더 체크섬 검증
		uint32_t src = swap32(ip.src_ip), dst = swap32(ip.dst_ip);
		uint16_t total = swap16(ip.total_length);
		// TODO: 조각(fragment) 재조립 — 지금은 완전한 패킷만
		string msg = packet.substr(ihl, total - ihl);         // transport 메시지 (IP 헤더 벗김)
		switch (ip.protocol) {
		case IPPROTO_ICMP:	ICMPSocket::deliver(src, dst, msg); break;
		case IPPROTO_UDP:	UDPSocket::deliver(src, dst, msg); break;
		case IPPROTO_TCP:  TCPSocket::deliver(src, dst, msg); break;
		}
	}
}
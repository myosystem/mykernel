#include "net/udp.h"
#include "net/ip.h"

static UDPSocket* ports[65536];
UDPSocket* UDPSocket::Create(uint64_t port) {
	if (port < sizeof(ports) / sizeof(*ports)) {
		if (ports[port] == nullptr) {
			UDPSocket* socket = new UDPSocket();
			ports[port] = socket;
			socket->udp_port_ = port;
			return socket;
		}
		return nullptr;
	}
	for (uint32_t i = 65535; i >= 100; i--) {
		if (!ports[i]) {               // 빈 슬롯 찾고 그때만 할당
			UDPSocket* socket = new UDPSocket();
			ports[i] = socket;
			socket->udp_port_ = i;
			return socket;
		}
	}
	return nullptr;
}
UDPSocket::~UDPSocket() {
	ports[udp_port_] = nullptr;
}
void UDPSocket::deliver(uint32_t src, uint32_t dst, string& msg) {
	if (msg.size() < sizeof(udp_header)) return;
	udp_header udp;
	msg.copy_out(0, (uint8_t*)&udp, sizeof(udp_header));
	UDPSocket* socket = ports[swap16(udp.dst_port)];
	if (socket) {
		RxMsg m;
		m.src_ip = src; m.dst_ip = dst;
		m.src_port = swap16(udp.src_port);
		m.dst_port = swap16(udp.dst_port);
		m.data = msg.substr(sizeof(udp_header), msg.size() - sizeof(udp_header));
		socket->recv_queue.enqueue(m);
		if (!socket->wait_queue.isEmpty() && (socket->state & SOCKET_FLAG_BLOCKING)) add_process(socket->wait_queue.dequeue());
		if (socket->state & SOCKET_FLAG_MESSAGE) {
			msg_t msg = {};
			msg.type = MSG_SOCKET_DATA;
			msg.payload.params.arg[0] = socket->id;
			msg.sender_pid = -1;
			GetProcess(socket->msg_pid)->msg_recv(msg, false);
		}
	}
}
void UDPSocket::sendto(uint32_t dst_ip, uint16_t dst_port, string& data, Route* route) {
	Route route_;
	if (route == nullptr) {
		bool ok = RouteTable::find(dst_ip, &route_);
		if (!ok) {
			return;
		}
		route = &route_;
	}
	if (route->dev == nullptr) {
		return;
	}
	udp_header header = { swap16(udp_port_), swap16(dst_port), swap16(data.size() + sizeof(udp_header)), 0};
	data.prepend((uint8_t*)&header, sizeof(header));
	pseudo_header pseudo;
	pseudo.src_ip = swap32(route->dev->src_ip());
	pseudo.dst_ip = swap32(dst_ip);
	pseudo.zero = 0;
	pseudo.protocol = IPPROTO_UDP;
	pseudo.length = header.length;
	uint16_t pseudo_ck = ip_checksum(&pseudo, sizeof(pseudo));
	uint16_t data_ck = data.checksum16(0, data.size());
	uint32_t combined = (uint16_t)~pseudo_ck + (uint16_t)~data_ck;
	while (combined >> 16) combined = (combined & 0xFFFF) + (combined >> 16);
	uint16_t ck = (uint16_t)~combined;
	if (ck == 0) ck = 0xFFFF;
	data.write_at(6, (const uint8_t*)&ck, sizeof(ck));
	IPv4::send(route, dst_ip, IPPROTO_UDP, data, ttl);
}
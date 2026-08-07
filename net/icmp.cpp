#include "net/icmp.h"
#include "net/ip.h"
static ICMPSocket* icmp_socket[65535];   // ICMP demux: icmp id -> socket
ICMPSocket* ICMPSocket::Create() {
	for (uint32_t i = 0; i < 65535; i++) {
		if (!icmp_socket[i]) {               // 빈 슬롯 찾고 그때만 할당
			ICMPSocket* socket = new ICMPSocket();
			icmp_socket[i] = socket;
			socket->icmp_id_ = i;
			return socket;
		}
	}
	return nullptr;
}
ICMPSocket::~ICMPSocket() {
	icmp_socket[icmp_id_] = nullptr;
}
void ICMPSocket::deliver(uint32_t src, uint32_t dst, string& msg) {
	if (msg.size() < sizeof(icmp_header)) return;
	icmp_header ic;
	msg.copy_out(0, (uint8_t*)&ic, sizeof(icmp_header));         // ICMP 헤더 벗기기
	if (ic.type == ICMP_ECHO_REQUEST) {
		// echo 데이터 + REPLY 헤더 (id/seq는 raw 바이트 그대로 echo)
		string reply = msg.substr(sizeof(icmp_header), msg.size() - sizeof(icmp_header));
		icmp_header header = { ICMP_ECHO_REPLY, 0, 0, ic.id, ic.seq };   // checksum=0
		reply.prepend((uint8_t*)&header, sizeof(icmp_header));
		uint16_t ck = reply.checksum16(0, reply.size());
		reply.write_at(2, (const uint8_t*)&ck, sizeof(ck));
		IPv4::send(src, IPPROTO_ICMP, reply, 64);                // 요청 src로 응답
		return;
	}
	// ECHO_REPLY 등: id로 소켓 demux
	ICMPSocket* socket = icmp_socket[swap16(ic.id)];
	if (socket) {
		RxMsg m;
		m.src_ip = src; m.dst_ip = dst;
		m.data = msg.substr(sizeof(icmp_header), msg.size() - sizeof(icmp_header));
		socket->recv_queue.enqueue(m);
		if (!socket->wait_queue.isEmpty()) add_process(socket->wait_queue.dequeue());
	}
}
void ICMPSocket::sendto(uint32_t dst_ip, string& data) {
	icmp_header header = { ICMP_ECHO_REQUEST, 0, 0, swap16(icmp_id_), swap16(seq++) };
	data.prepend((uint8_t*)&header, sizeof(header));         // ICMP 헤더 앞에 (복사 0)
	header.checksum = data.checksum16(0, data.size());
	data.write_at(2, (const uint8_t*)&header.checksum, sizeof(header.checksum));
	IPv4::send(dst_ip, IPPROTO_ICMP, data, ttl);
}
void ICMPSocket::recv(uint32_t& src_ip, uint32_t& dst_ip, string& data) {
	while (recv_queue.isEmpty()) {
		wait_queue.enqueue(now_process->id);
		now_process->state |= PROCESS_STATE_SOCKET_WAIT;
		simple_wait();
		now_process->state = now_process->state & ~PROCESS_STATE_SOCKET_WAIT;
	}
	RxMsg m = recv_queue.dequeue();
	src_ip = m.src_ip; dst_ip = m.dst_ip;
	data = (string&&)m.data;   // move
}
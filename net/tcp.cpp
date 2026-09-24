#include "net/tcp.h"
#include "arch/lapic.h"
uint64_t TCPSocket::TCP_WINDOW = 65535;
static TCPSocket* ports[65536];
TCPSocket* TCPSocket::Create(uint64_t port) {
	if (port < sizeof(ports) / sizeof(*ports)) {
		if (ports[port] == nullptr) {
			TCPSocket* socket = new TCPSocket();
			ports[port] = socket;
			socket->tcp_port_ = port;
			return socket;
		}
		return nullptr;
	}
	for (uint32_t i = 65535; i >= 100; i--) {
		if (!ports[i]) {               // 빈 슬롯 찾고 그때만 할당
			TCPSocket* socket = new TCPSocket();
			ports[i] = socket;
			socket->tcp_port_ = i;
			return socket;
		}
	}
	return nullptr;
}
void TCPSocket::make_tcp_header(uint32_t seq, uint8_t flags, string& data, uint64_t optlen, uint16_t src_port, uint32_t dst_ip, uint16_t dst_port) {
	tcp_header header;
	header.src_port = swap16(src_port);
	header.dst_port = swap16(dst_port);
	header.seq = swap32(seq);
	header.ack = swap32(recvbuf.ack());
	header.offset = ((sizeof(tcp_header) + optlen) / 4) << 4;
	header.flags = flags;
	header.window = swap16(TCP_WINDOW - recvbuf.length());
	header.checksum = 0;
	header.urgent_ptr = 0;
	data.prepend((const uint8_t*)&header, sizeof(header));
	pseudo_header pseudo;
	pseudo.src_ip = swap32(route.dev->src_ip());
	pseudo.dst_ip = swap32(dst_ip);
	pseudo.zero = 0;
	pseudo.protocol = IPPROTO_TCP;
	pseudo.length = swap16(data.size());
	uint16_t pseudo_ck = ip_checksum(&pseudo, sizeof(pseudo));
	uint16_t data_ck = data.checksum16(0, data.size());
	uint32_t combined = (uint16_t)~pseudo_ck + (uint16_t)~data_ck;
	while (combined >> 16) combined = (combined & 0xFFFF) + (combined >> 16);
	uint16_t ck = (uint16_t)~combined;
	data.write_at(16, (const uint8_t*)&ck, sizeof(ck));
}
bool TCPSocket::connect(uint32_t dst_ip, uint16_t dst_port) {
	bool ok = RouteTable::find(dst_ip, &route);
	if (!ok) {
		return false;
	}
	if (route.dev == nullptr) {
		return false;
	}
	iss = rdtsc_get();
	dst_ip_ = dst_ip;
	dst_port_ = dst_port;
	tcp_state = TCP_SYN_SENT;
	string data;
	uint16_t mss = route.dev->max_packet_size() - sizeof(ipv4_header) - sizeof(tcp_header);
	uint8_t opts[4] = { 2, 4, (uint8_t)(mss >> 8), (uint8_t)(mss & 0xFF) };
	sendbuf.init(iss + 1);
	data.prepend(opts, sizeof(opts));
	make_tcp_header(iss, TCP_SYN, data, sizeof(opts), tcp_port_, dst_ip_, dst_port_);
	IPv4::send(&route, dst_ip, IPPROTO_TCP, data, ttl);
	while (tcp_state == TCP_SYN_SENT && (state & SOCKET_FLAG_BLOCKING)) {
		wait_queue.enqueue(now_process->id);
		now_process->state |= PROCESS_STATE_SOCKET_WAIT;
		simple_wait();
		now_process->state = now_process->state & ~PROCESS_STATE_SOCKET_WAIT;
	}
	return tcp_state == TCP_ESTABLISHED;
}
void TCPSocket::sendto(uint32_t dst_ip, uint16_t dst_port, string& data, Route* route) {
	if (tcp_state != TCP_ESTABLISHED) return;

	sendbuf.insert(data, false);

	uint16_t mss = this->route.dev->max_packet_size()
		- sizeof(ipv4_header) - sizeof(tcp_header);

	while (sendbuf.length_send()) {
		uint32_t seq = sendbuf.seq_send();
		string seg;
		sendbuf.read_sendbuf(seg, mss);
		make_tcp_header(seq, TCP_PSH | TCP_ACK, seg, 0,
			tcp_port_, dst_ip_, dst_port_);
		IPv4::send(&this->route, dst_ip_, IPPROTO_TCP, seg, ttl);
	}
}
uint64_t TCPSocket::recv(RxMsg& data, size_t& maxlen) {
	if (state & SOCKET_FLAG_BLOCKING) {
		while (!recvbuf.readable()) {
			if (tcp_state == TCP_CLOSED) return 0; // EOF
			wait_queue.enqueue(now_process->id);
			now_process->state |= PROCESS_STATE_SOCKET_WAIT;
			uint64_t r = simple_wait();
			if (r == -1) {
				return -1; // 긴급탈출
			}
			now_process->state = now_process->state & ~PROCESS_STATE_SOCKET_WAIT;
		}
	}
	else {
		if (!recvbuf.readable()) {
			return -1;
		}
	}
	RxMsg m;
	maxlen = recvbuf.read(m.data, maxlen);
	m.src_ip = dst_ip_;
	m.src_port = dst_port_;
	m.dst_ip = route.dest;
	m.dst_port = tcp_port_;
	data = (RxMsg&&)m;
	return recvbuf.readable();
}
void TCPSocket::deliver(uint32_t src, uint32_t dst, string& msg) {
	if (msg.size() < sizeof(tcp_header)) return;
	tcp_header tcp;
	msg.copy_out(0, (uint8_t*)&tcp, sizeof(tcp_header));
	TCPSocket* socket = ports[swap16(tcp.dst_port)];
	if (socket) {
		if (tcp.flags & TCP_RST) {
			socket->tcp_state = TCP_CLOSED;
			if (!socket->wait_queue.isEmpty() && (socket->state & SOCKET_FLAG_BLOCKING)) add_process(socket->wait_queue.dequeue(), 0);
			if (socket->state & SOCKET_FLAG_MESSAGE) {
				msg_t msg = {};
				msg.type = MSG_SOCKET_DATA;
				msg.payload.params.arg[0] = socket->id;
				msg.sender_pid = -1;
				GetProcess(socket->msg_pid)->msg_recv(msg, false);
			}
			return;
		}
		if (tcp.flags & TCP_ACK)
			socket->sendbuf.ack(swap32(tcp.ack));
		if (socket->tcp_state == TCP_SYN_SENT) {
			if (!(tcp.flags & TCP_SYN) || !(tcp.flags & TCP_ACK) || (swap32(tcp.ack) != socket->iss + 1)) {
				string data;
				socket->make_tcp_header(socket->sendbuf.seq_send(), TCP_RST, data, 0, socket->tcp_port_, socket->dst_ip_, socket->dst_port_);
				IPv4::send(&socket->route, socket->dst_ip_, IPPROTO_TCP, data, socket->ttl);
				socket->tcp_state = TCP_CLOSED;
				return;
			}
			else {
				socket->recvbuf.init(swap32(tcp.seq) + 1, TCP_WINDOW);
				string data;
				socket->make_tcp_header(socket->sendbuf.seq_send(), TCP_ACK, data, 0, socket->tcp_port_, socket->dst_ip_, socket->dst_port_);
				IPv4::send(&socket->route, socket->dst_ip_, IPPROTO_TCP, data, socket->ttl);
				socket->tcp_state = TCP_ESTABLISHED;
				if (!socket->wait_queue.isEmpty() && (socket->state & SOCKET_FLAG_BLOCKING)) add_process(socket->wait_queue.dequeue(),0);
				if (socket->state & SOCKET_FLAG_MESSAGE) {
					msg_t msg = {};
					msg.type = MSG_SOCKET_DATA;
					msg.payload.params.arg[0] = socket->id;
					msg.sender_pid = -1;
					GetProcess(socket->msg_pid)->msg_recv(msg, false);
				}
				return;
			}
		}
		uint32_t hlen = (tcp.offset >> 4) * 4;
		if (hlen < sizeof(tcp_header) || hlen > msg.size()) return;
		uint32_t seglen = msg.size() - hlen;
		if (seglen == 0) return;
		socket->recvbuf.insert(swap32(tcp.seq), msg.substr(hlen, seglen));

		string a;
		socket->make_tcp_header(socket->sendbuf.seq_send(), TCP_ACK, a, 0,
			socket->tcp_port_, socket->dst_ip_, socket->dst_port_);
		IPv4::send(&socket->route, socket->dst_ip_, IPPROTO_TCP, a, socket->ttl);

		if (!socket->recvbuf.length()) return;
		if (!socket->wait_queue.isEmpty() && (socket->state & SOCKET_FLAG_BLOCKING)) add_process(socket->wait_queue.dequeue(), 0);
		if (socket->state & SOCKET_FLAG_MESSAGE) {
			msg_t msg = {};
			msg.type = MSG_SOCKET_DATA;
			msg.payload.params.arg[0] = socket->id;
			msg.sender_pid = -1;
			GetProcess(socket->msg_pid)->msg_recv(msg, false);
		}
	}
}
void TCPSocket::close() {

}
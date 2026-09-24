#ifndef __TCP_H__
#define __TCP_H__
#include "util/size.h"
#include "util/string.h"
#include "net/socket.h"
struct tcp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;          // 이 세그먼트 첫 바이트의 번호
    uint32_t ack;          // "여기까지 받았음, 다음은 이 번호부터 줘"
    uint8_t  offset;       // 상위 4비트 = 헤더 길이(4바이트 단위). 하위 4비트 예약
    uint8_t  flags;        // 아래 플래그 조합
    uint16_t window;       // 내 수신 버퍼 여유 = rwnd
    uint16_t checksum;     // 의사헤더 + TCP헤더 + 데이터
    uint16_t urgent_ptr;   // 사실상 안 씀. 0
} __attribute__((packed));

#define TCP_FIN  0x01
#define TCP_SYN  0x02
#define TCP_RST  0x04
#define TCP_PSH  0x08
#define TCP_ACK  0x10
#define TCP_URG  0x20
#define TCP_ECE  0x40   // ECN: 혼잡 겪었다고 상대에게 알림
#define TCP_CWR  0x80   // ECN: cwnd 줄였다고 응답
class TCPRecvBuffer {
private:
    struct Hole {
        int32_t offset;
        uint32_t size;
    };
    string buf;
    vector<Hole> holes;
    int32_t offset;
    uint32_t window;
public:
    TCPRecvBuffer() : offset(-1) {}
    void init(int32_t offset, uint32_t window) {
        if (this->offset != -1) return;
        this->offset = offset;
        this->window = window;
        holes.push_back({ offset,window});
    }
    void insert(int32_t offset, const string& data) {
        for (size_t i = 0; i < holes.size(); i++) {
            Hole& hole = holes[i];
            int32_t left = (offset - hole.offset > 0 ? offset : hole.offset);
            int32_t right = ((int32_t)((offset + data.size()) - (hole.offset + hole.size)) > 0
                ? hole.offset + hole.size : offset + data.size());
            if (left - right >= 0) continue; // 포함 안되는 경우
            if ((int32_t)(right - this->offset - (uint32_t)buf.size()) > 0)
                buf.pad((int32_t)(right - this->offset - (uint32_t)buf.size()));
            if (i != holes.size() - 1 && left == hole.offset && right == hole.offset + hole.size) {      // 구멍이 더 작은 경우
                buf.write_at(hole.offset - this->offset, data.substr(hole.offset - offset, hole.size));
                holes.erase(i);
                i--;
                continue;
            }
            else if (left == hole.offset) {     // 데이터가 오른쪽이 부족한 경우
                buf.write_at(hole.offset - this->offset, data.substr(hole.offset - offset, right - left));
                hole.offset += right - left;
                hole.size -= right - left;
                continue;
            }
            else if (i != holes.size() - 1 && right == hole.offset + hole.size) {     // 데이터가 왼쪽이 부족한 경우
                buf.write_at(offset - this->offset, data.substr(0, right - left));
                hole.size -= right - left;
                continue;
            }
            else {     // 데이터가 양쪽 다 부족한 경우
                holes.insert({ right, (uint32_t)((hole.offset + hole.size) - right)}, i + 1);
                hole.size = left - hole.offset;
                buf.write_at(left - this->offset, data);
                break;
            }
        }
    }
    uint64_t read(string& str, uint64_t maxlen) {
        uint32_t readable = holes[0].offset - this->offset;   // 첫 구멍 앞까지가 연속
        uint32_t n = (maxlen < readable) ? maxlen : readable;
        if (n == 0) return 0;
        str = buf.substr(0, n);
        buf = buf.substr(n, buf.size() - n);                  // 앞 n 소비
        this->offset += n;
        holes[holes.size() - 1].size += n;                    // 꼬리 늘려 window 회복
        return n;
    }
    uint32_t length() const {
        return buf.size();
    }
    uint32_t ack() const {
        if (offset == -1) return 0;
        return holes[0].offset;
    }
    uint32_t readable() const {
        if (offset == -1) return 0;
        return holes[0].offset - offset;
    }
};
class TCPSendBuffer {
private:
    string buf;
    int32_t offset;
    int32_t send_offset;
public:
    TCPSendBuffer() : offset(-1), send_offset(-1) {}
    void init(int32_t offset) {
        if (this->offset != -1) return;
        this->offset = offset;
        this->send_offset = offset;
    }
    void insert(const string& data, bool is_sended = true) {
        buf.append_from(data, 0, data.size());
        if (is_sended)
            send_offset += data.size();
    }
    uint64_t read_ack(string& str, uint64_t maxlen) {
        uint32_t n = (maxlen < buf.size()) ? maxlen : buf.size();
        str = buf.substr(0, n);
        if (offset + n > send_offset) send_offset = offset + n;
        return n;
    }
    uint64_t read_sendbuf(string& str, uint64_t maxlen) {
        uint32_t n = (maxlen < (buf.size() - (send_offset - offset))) ? maxlen : (buf.size() - (send_offset - offset));
        str = buf.substr(send_offset - offset, n);
        send_offset += n;
        return n;
    }
    void ack(int32_t ack) {
        if (ack <= offset) return;
        buf = buf.substr(ack - offset, buf.size() - (ack - offset));
        offset = ack;
    }
    uint32_t length_ack() const {
        return buf.size();
    }
    uint32_t length_send() const {
        return buf.size() - (send_offset - offset);
    }
    int32_t seq_send() const {
        return send_offset;
    }
    int32_t seq_ack() const {
        return offset;
    }
};
class TCPSocket : public Socket {
private:
    enum TCPState {
        TCP_CLOSED,
        TCP_SYN_SENT,
        TCP_ESTABLISHED,
    };
    uint16_t tcp_port_;
    uint32_t iss;
    static uint64_t TCP_WINDOW;
    TCPState tcp_state;
    TCPRecvBuffer recvbuf;
    TCPSendBuffer sendbuf;
    void make_tcp_header(uint32_t seq, uint8_t flags, string& data, uint64_t optlen, uint16_t src_port, uint32_t dst_ip, uint16_t dst_port);
public:
    TCPSocket() {}
    static TCPSocket* Create(uint64_t port);
    bool connect(uint32_t dst_ip, uint16_t dst_port) override;
    void sendto(uint32_t dst_ip, uint16_t dst_port, string& data, Route* route = nullptr);
    uint64_t recv(RxMsg& data, size_t& maxlen) override;
    static void deliver(uint32_t src, uint32_t dst, string& msg);
    void close();
};
#endif // __TCP_H__
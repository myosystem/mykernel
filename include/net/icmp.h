#ifndef __ICMP_H__
#define __ICMP_H__
#include "util/size.h"
#include "net/socket.h"
#include "net/netdevice.h"
// ICMP type
#define ICMP_ECHO_REPLY     0
#define ICMP_ECHO_REQUEST   8
#define ICMP_DEST_UNREACH   3
#define ICMP_TIME_EXCEEDED  11
// code (DEST_UNREACH)
#define ICMP_NET_UNREACH    0
#define ICMP_HOST_UNREACH   1
#define ICMP_PORT_UNREACH   3
#define ICMP_FRAG_NEEDED    4
// code (TIME_EXCEEDED)
#define ICMP_TTL_EXPIRED    0
struct icmp_header {
    uint8_t  type;        // 8=echo request(핑 요청), 0=echo reply(응답)
    uint8_t  code;        // 0
    uint16_t checksum;    // ICMP 메시지 전체 체크섬
    uint16_t id;          // 식별자 (어느 핑인지 = 소켓 키)
    uint16_t seq;         // 시퀀스 번호
} __attribute__((packed));

class ICMPSocket : public Socket {
private:
    uint16_t icmp_id_;                 // ICMP demux 키 (icmp_socket[] 인덱스)
    ICMPSocket() : Socket() {}        // type_=1 (ICMP)
protected:
    uint16_t seq = 0;
    void sendto(uint32_t dst_ip, uint16_t dst_port, string& data, Route* route = nullptr) override;
public:
    ~ICMPSocket() override;
    static ICMPSocket* Create();
    static void deliver(uint32_t src, uint32_t dst, string& msg);   // ICMP 헤더 벗기고 echo응답/소켓demux
};
#endif // __ICMP_H__
#ifndef __IP_H__
#define __IP_H__

#include "util/size.h"
#include "util/string.h"
#define IPV4_VERSION 4
#define IPV4_FLAGS_DF 0x4000
#define IPV4_FRAG_MF 0x2000
// IP protocol 번호 (와이어)
#define IPPROTO_ICMP  1
#define IPPROTO_TCP   6
#define IPPROTO_UDP   17
struct ipv4_header {
    uint8_t  version_ihl;    // 상위4비트=버전(4), 하위4비트=IHL
    uint8_t  tos;            // type of service (보통 0)
    uint16_t total_length;   // 전체 길이(헤더+데이터), big-endian
    uint16_t id;             // 식별자 (fragmentation용)
    uint16_t flags_frag;     // 상위3=플래그, 하위13=fragment offset
    uint8_t  ttl;            // time to live (64)
    uint8_t  protocol;       // 1=ICMP, 6=TCP, 17=UDP
    uint16_t checksum;       // 헤더 체크섬
    uint32_t src_ip;         // 출발지 IP (network order)
    uint32_t dst_ip;         // 목적지 IP
} __attribute__((packed));
struct Route;
uint16_t ip_checksum(const void* data, int len);
uint32_t ipaddr(uint8_t a, uint8_t b, uint8_t c, uint8_t d);
uint16_t ip_next_id();
namespace IPv4 {
    void send(Route* route, uint32_t dst, uint8_t proto, string& payload, uint8_t ttl);
    void recv(string& packet);   // IP 헤더 벗기고 protocol로 상위 계층에 demux
}
#endif // __IP_H__
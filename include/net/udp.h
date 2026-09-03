#ifndef __UDP_H__
#define __UDP_H__
#include "net/socket.h"
struct udp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
};

class UDPSocket : public Socket {
private:
    uint16_t udp_port_;              // port
    UDPSocket() : Socket() {}        // type_=3 (UDP)
protected:
    uint16_t seq = 0;
    void sendto(uint32_t dst_ip, uint16_t dst_port, string& data, Route* route = nullptr) override;
public:
    ~UDPSocket() override;
    static UDPSocket* Create(uint64_t port = -1);
    static void deliver(uint32_t src, uint32_t dst, string& msg);
    void close() override;
};

#endif // __UDP_H__
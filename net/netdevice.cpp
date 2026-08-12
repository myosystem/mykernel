#include "net/netdevice.h"
#include "util/vector.h"
#include "net/ip.h"
#include "net/icmp.h"
namespace RouteTable {
	static vector<Route>* routes;
	alignas(vector<Route>) static uint8_t routes_buf[sizeof(*routes)];
	alignas(LoopBack) static uint8_t loopback_buf[sizeof(LoopBack)];
	void init() {
		routes = new (routes_buf) vector<Route>();
		NetDevice* loopback_dev = new (loopback_buf) LoopBack();
		RouteTable::add(ipaddr(127, 0, 0, 0), ipaddr(255, 0, 0, 0), 0, loopback_dev);
	}
	void add(uint32_t dest, uint32_t netmask, uint32_t gateway, NetDevice* dev) {
		routes->push_back({ dest, netmask, gateway, dev });
		if (gateway != 0) {
			routes->push_back({ ipaddr(0,0,0,0),ipaddr(0,0,0,0),gateway,dev });
		}
	}
	bool find(uint32_t ip, Route* route_out) {
		Route* result = nullptr;
		for (int i = 0; i < routes->size(); i++) {
			if ((ip & (*routes)[i].netmask) == ((*routes)[i].dest & (*routes)[i].netmask)) {
				if (!result || (*routes)[i].netmask > result->netmask) {
					result = &(*routes)[i];
				}
			}
		}
		if (result) {
			*route_out = *result;
			return true;
		}
		else {
			return false;
		}
	}
}
void LoopBack::send(string& packet, uint32_t next_hop, uint16_t ethertype) {
	IPv4::recv(packet);   // frameless: IP 패킷을 IP recv로 바로 (next_hop/ethertype 무시)
}
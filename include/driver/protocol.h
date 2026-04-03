#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__
#include "util/size.h"
#define PROTOCOL_QUEUE_BASE 0xFFFF858000000000ULL
#define PROTOCOL_STRUCT_SIZE 0x100
class Protocol {
private:
	uint64_t state;
	uint64_t protocol_id;
public:
	void* operator new(size_t size);
	void operator delete(void* ptr);
};

#endif // __PROTOCOL_H__
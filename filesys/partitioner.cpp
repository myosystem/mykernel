#include "filesys/partitioner.h"
#include "filesys/mbr.h"
#include "util/size.h"

Partitioner* Partitioner::create_default() {
	// 기본 파티셔너는 MBR 파티셔너
	return new MBRPartitioner();
}
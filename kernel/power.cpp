#include "kernel/power.h"
#include "arch/io.h"
#include "driver/disk.h"
extern bool booting;
bool power_off() {
	outw(0x604, 0x2000);
	return false;
}
bool shutdown() {
	booting = true;
	for (int i = 0; i < disks->size(); i++) {
		(*disks)[i]->cleanup();
		delete (*disks)[i];
	}
	return power_off();
}
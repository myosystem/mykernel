#ifndef __GPT_H__
#define __GPT_H__
#include "util/size.h"
#include "driver/ahci.h"
uint16_t init_gpt(volatile HBA_PORT* port, void* header);
#endif
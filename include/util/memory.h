#ifndef __MEMORY_H__
#define __MEMORY_H__
extern "C" __attribute__((no_caller_saved_registers))
void* memcpy(void* dest, const void* src, unsigned long long size);

extern "C" __attribute__((no_caller_saved_registers))
void* memset(void* dest, int value, unsigned long long size);

extern "C" int strcmp(const char* s1, const char* s2);

extern "C" int strncmp(const char* s1, const char* s2, unsigned long long n);

extern "C" int strcasecmp(const char* s1, const char* s2);

extern "C" void* strncpy(char* dest, const char* src, unsigned long long n);
#endif /* __MEMORY_H__ */
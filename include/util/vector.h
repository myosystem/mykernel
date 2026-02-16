#ifndef __VECTOR_H__
#define __VECTOR_H__
#include "util/size.h"
template <typename T>
class vector {
public:
    vector();
    ~vector();

    void push_back(const T& item);
    void erase(size_t index);
    T& operator[](size_t index);
    size_t size() const;

private:
    void* front_page;
    void* back_page;
    size_t new_index;
};

#endif // __VECTOR_H__
#ifndef TRACE_EVENT_H
#define TRACE_EVENT_H

#include <stdint.h>

void trace_set_thread_name(const char* name);
void trace_begin(const char* name);
void trace_end(const char* name);
void trace_save(const char* filename);
uint64_t trace_now_us();

#endif // TRACE_EVENT_H

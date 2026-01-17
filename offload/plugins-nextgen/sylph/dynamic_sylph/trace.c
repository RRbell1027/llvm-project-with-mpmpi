#ifndef TRACE_EVENT_H
#define TRACE_EVENT_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

#include "trace.h"

#define MAX_EVENTS 8192
#define MAX_EVENT_STR 512

static char* events[MAX_EVENTS];
static int event_count = 0;
static pthread_mutex_t event_mutex = PTHREAD_MUTEX_INITIALIZER;

static void add_event(const char* event) {
    pthread_mutex_lock(&event_mutex);
    if (event_count < MAX_EVENTS) {
        events[event_count++] = strdup(event);
    }
    pthread_mutex_unlock(&event_mutex);
}

uint64_t trace_now_us() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static unsigned long get_thread_id() {
    return (unsigned long)pthread_self();
}

void trace_set_thread_name(const char* name) {
    char buf[MAX_EVENT_STR];
    snprintf(buf, sizeof(buf),
        "{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":1,\"tid\":%lu,\"args\":{\"name\":\"%s\"}}",
        get_thread_id(), name);
    add_event(buf);
}

void trace_begin(const char* name) {
    uint64_t ts = trace_now_us();
    char buf[MAX_EVENT_STR];
    snprintf(buf, sizeof(buf),
        "{\"name\":\"%s\",\"ph\":\"B\",\"ts\":%lu,\"pid\":1,\"tid\":%lu}",
        name, ts, get_thread_id());
    add_event(buf);
}

void trace_end(const char* name) {
    uint64_t ts = trace_now_us();
    char buf[MAX_EVENT_STR];
    snprintf(buf, sizeof(buf),
        "{\"name\":\"%s\",\"ph\":\"E\",\"ts\":%lu,\"pid\":1,\"tid\":%lu}",
        name, ts, get_thread_id());
    add_event(buf);
}

void trace_save(const char* filename) {
    FILE* f = fopen(filename, "w");
    if (!f) return;

    fprintf(f, "{ \"traceEvents\": [\n");
    for (int i = 0; i < event_count; ++i) {
        fprintf(f, "%s", events[i]);
        if (i + 1 < event_count) fprintf(f, ",");
        fprintf(f, "\n");
        free(events[i]);  // cleanup
    }
    fprintf(f, "]}\n");
    fclose(f);
    event_count = 0;
}

#endif // TRACE_EVENT_H
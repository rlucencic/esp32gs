/*
 * esp32gs: in place of Clemens's clem_debug.c (its debugger keeps a 6.6 MB
 * trace buffer). Messages go to the machine's log function, at or above
 * clem_debug_log_level; breaks and traces are not used.
 */
#include <stdarg.h>
#include <stdio.h>

#include "clem_debug.h"
#include "clem_shared.h"

int clem_debug_log_level = CLEM_DEBUG_LOG_WARN;
void (*clem_debug_log_fn)(int level, const char *msg);

void clem_debug_reset(struct ClemensDeviceDebugger *dbg) { (void)dbg; }

void clem_debug_break(struct ClemensDeviceDebugger *dbg, unsigned debug_reason, unsigned param0,
                      unsigned param1) {
    (void)dbg;
    clem_debug_log(CLEM_DEBUG_LOG_DEBUG, "unimplemented I/O %s $%04X (%02X)",
                   debug_reason == 1 ? "read" : "write", param0, param1);
}

void clem_debug_context(ClemensMachine *context) { (void)context; }

/* Each message (by its format) at most a few times: some come every frame */
#define SEEN_FORMATS 64
#define SEEN_TIMES   3

static int seen(const char *fmt) {
    static const char *formats[SEEN_FORMATS];
    static unsigned char times[SEEN_FORMATS];
    int i;
    for (i = 0; i < SEEN_FORMATS && formats[i]; i++) {
        if (formats[i] == fmt) {
            return times[i] < 255 ? ++times[i] : 255;
        }
    }
    if (i < SEEN_FORMATS) {
        formats[i] = fmt;
        times[i] = 1;
        return 1;
    }
    return SEEN_TIMES + 1;
}

void clem_debug_log(int log_level, const char *fmt, ...) {
    char msg[160];
    va_list args;
    if (log_level < clem_debug_log_level || seen(fmt) > SEEN_TIMES) {
        return;
    }
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    if (clem_debug_log_fn) {
        (*clem_debug_log_fn)(log_level, msg);
    } else {
        fprintf(stderr, "clemens: %s\n", msg);
    }
}

char *clem_debug_acquire_trace(unsigned amt) {
    (void)amt;
    return NULL;
}

void clem_debug_trace_flush(void) {}

void clemens_debug_status_toolbox(ClemensMachine *context, unsigned id) {
    (void)context;
    (void)id;
}

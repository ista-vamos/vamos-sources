#ifndef DRFUN_EVENT_SPEC_H_
#define DRFUN_EVENT_SPEC_H_

#include <stddef.h>
#include <stdint.h>

struct call_event_spec {
    char name[256]; /* name */
    size_t addr;    /* offset in module */

    /* string describing what arguments to track and what is their size:
     * c = (unsigned) char
     * s = (unsigned) short
     * i = (unsigned) int
     * l = (unsigned) long
     * f = float
     * d = double
     * p = pointer
     * S = string (0-terminated array of chars)
     * _ = skip argument
     * E.g.: "i_c" means track first and third arguments that have 4 bytes
     * and 1 byte size
     * */
    unsigned char signature[8]; /* for now, we allow 8 arguments at most */
    /* the type of event assigned by the monitor */
    uint64_t kind;
    /* size in the buffer */
    size_t size;
};

#endif /* DRFUN_EVENTS_H_ */

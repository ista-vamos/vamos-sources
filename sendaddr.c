#include <assert.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "event.h"
#include "shmbuf/buffer.h"
#include "shmbuf/client.h"
#include "signatures.h"
#include "source.h"

#define MAXMATCH 20

static void usage_and_exit(int ret) {
    fprintf(stderr, "Usage: shmkey N\n");
    exit(ret);
}

#define WITH_STDOUT
#ifndef WITH_STDOUT
#define printf(...) \
    do {            \
    } while (0)
#endif

struct event {
    shm_event base;
    unsigned char args[];
};

static size_t waiting_for_buffer = 0;

int main(int argc, char *argv[]) {
    if (argc != 3) {
        usage_and_exit(1);
    }
    const char *shmkey = argv[1];
    long N = atol(argv[2]);

    /* Initialize the info about this source */
    struct source_control *control = source_control_define(1, "addr", "p");
    assert(control);

    const size_t capacity = 128;
    struct buffer *shm = create_shared_buffer(shmkey, capacity, control);
    assert(shm);
    free(control);

    fprintf(stderr, "info: waiting for the monitor to attach... ");
    buffer_wait_for_monitor(shm);
    fprintf(stderr, "done\n");

    size_t events_num;
    struct event_record *events = buffer_get_avail_events(shm, &events_num);
    assert(events_num == 1);

    struct event ev;
    ev.base.id = 0;
    ev.base.kind = events[0].kind;
    void *addr, *p;
    while (--N > 0) {
        while (!(addr = buffer_start_push(shm))) {
            ++waiting_for_buffer;
        }
        ++ev.base.id;
        p = addr;
        addr = buffer_partial_push(shm, addr, &ev, sizeof(ev));
        buffer_partial_push(shm, addr, &p, sizeof(void *));
        buffer_finish_push(shm);
    }

    fprintf(stderr, "info: sent %lu events, busy waited on buffer %lu cycles\n",
            ev.base.id, waiting_for_buffer);
    destroy_shared_buffer(shm);

    return 0;
}

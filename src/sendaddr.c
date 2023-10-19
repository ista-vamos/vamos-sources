#include <assert.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vamos-buffers/core/event.h"
#include "vamos-buffers/shmbuf/buffer.h"
#include "vamos-buffers/shmbuf/client.h"
#include "vamos-buffers/core/signatures.h"
#include "vamos-buffers/core/source.h"

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
    vms_event base;
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
    struct vms_source_control *control =
        vms_source_control_define(1, "addr", "p");
    assert(control);

    const size_t capacity = 128;
    vms_shm_buffer *shm = vms_shm_buffer_create(shmkey, capacity, control);
    assert(shm);
    free(control);

    fprintf(stderr, "info: waiting for the monitor to attach... ");
    vms_shm_buffer_wait_for_reader(shm);
    fprintf(stderr, "done\n");

    size_t events_num;
    struct vms_event_record *events =
        vms_shm_buffer_get_avail_events(shm, &events_num);
    assert(events_num == 1);

    struct event ev;
    ev.base.id = 0;
    ev.base.kind = events[0].kind;
    void *addr, *p;
    while (--N > 0) {
        while (!(addr = vms_shm_buffer_start_push(shm))) {
          ++waiting_for_buffer;
        }
        ++ev.base.id;
        p = addr;
        addr = vms_shm_buffer_partial_push(shm, addr, &ev, sizeof(ev));
        vms_shm_buffer_partial_push(shm, addr, &p, sizeof(void *));
        vms_shm_buffer_finish_push(shm);
    }

    fprintf(stderr, "info: sent %lu events, busy waited on buffer %lu cycles\n",
            ev.base.id, waiting_for_buffer);
    vms_shm_buffer_destroy(shm);

    return 0;
}

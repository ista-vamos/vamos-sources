#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>
#include <time.h>
#include <signal.h>

/* RDTSC */
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

#undef DEBUG_STDOUT
#ifdef DEBUG_STDOUT
#include <stdio.h>
#endif

#include "vamos-buffers/core/list-embedded.h"
#include "vamos-buffers/core/source.h"
#include "vamos-buffers/core/utils.h"
#include "vamos-buffers/shmbuf/buffer.h"
#include "vamos-buffers/shmbuf/client.h"

static CACHELINE_ALIGNED _Atomic size_t last_thread_id = 1;
static CACHELINE_ALIGNED _Atomic size_t timestamp = 1;

static struct buffer *top_shmbuf;
static struct source_control *top_control;

struct __vrd_thread_data {
    /* The original data passed to thrd_create */
    void *data;
    /* Our internal unique thread ID */
    uint64_t thread_id;
    /* ID assigned by the thrd_create/ptrhead_create function
     * (might not be uinque) */
    uint64_t std_thread_id;
    /* SHM buffer */
    struct buffer *shmbuf;
    /* the thread exited? */
    bool exited;

    shm_list_embedded list;
};

/* thread local data */
static CACHELINE_ALIGNED _Thread_local struct _thread_data {
    size_t thread_id;
    shm_eventid last_id;
    struct buffer *shmbuf;
    struct __vrd_thread_data *data;
    size_t waited_for_buffer;
} thread_data;

#ifdef DEBUG_STDOUT
static inline uint64_t rt_timestamp(void) { return __rdtsc(); }
#endif

const char *shmkey = "/vrd";
static struct buffer *top_shmbuf;
#ifdef DBGBUF
const char *dbgkey = "/vrd-vars";
static size_t dbgbuf_size;
static const size_t dbgbuf_capacity = 400;
static const size_t dbgbuf_key_size = sizeof(void*);
static const size_t dbgbuf_value_size = 24;
static vms_shm_dbg_buffer *dbgbuf;
#endif

#define EVENTS_NUM 12
enum {
    EV_READ = 0,
    EV_WRITE = 1,
    EV_ATOMIC_READ = 2,
    EV_ATOMIC_WRITE = 3,
    EV_LOCK = 4,
    EV_UNLOCK = 5,
    EV_ALLOC = 6,
    EV_FREE = 7,
    EV_FORK = 8,
    EV_JOIN = 9,
    EV_WRITE_N = 10,
    EV_READ_N = 11
};

/* local cache */
uint64_t event_kinds[EVENTS_NUM];

shm_list_embedded data_list = {&data_list, &data_list};

static void (*old_sigabrt_handler)(int);
static void (*old_sigiot_handler)(int);
static void (*old_sigsegv_handler)(int);

static void sig_handler(int sig) {
    printf("signal %d caught...\n", sig);

    /* restore previous handlers */
    signal(SIGABRT, old_sigabrt_handler);
    signal(SIGIOT, old_sigiot_handler);
    signal(SIGSEGV, old_sigsegv_handler);

    if (top_shmbuf) {
	fprintf(stderr, "info: number of emitted events: %lu\n", timestamp - 1);
        destroy_shared_buffer(top_shmbuf);
        top_shmbuf = NULL;
    }
#ifdef DBGBUF
    if (dbgbuf) {
	vms_shm_dbg_buffer_release(dbgbuf);
	dbgbuf = NULL;
    }
#endif
}

static void setup_signals() {
    old_sigabrt_handler = signal(SIGABRT, sig_handler);
    if (old_sigabrt_handler == SIG_ERR) {
        perror("failed setting SIGABRT handler");
    }

    old_sigiot_handler = signal(SIGIOT, sig_handler);
    if (old_sigiot_handler == SIG_ERR) {
        perror("failed setting SIGIOT handler");
    }

    old_sigsegv_handler = signal(SIGSEGV, sig_handler);
    if (old_sigsegv_handler == SIG_ERR) {
        perror("failed setting SIGSEGV handler");
    }
}

void __vrd_print_var(void *addr, const char *name) {
#ifdef DBGBUF
    fprintf(stderr, "[dbg] addr %p -> '%s'\n", addr, name);
    if (dbgbuf) {
        if (dbgbuf_size >= dbgbuf_capacity)
                return;
        assert(vms_shm_dbg_buffer_size(dbgbuf) == dbgbuf_size);

        unsigned char *data
		= vms_shm_dbg_buffer_data(dbgbuf) + dbgbuf_size*(dbgbuf_key_size+dbgbuf_value_size);
        memcpy((char*)data, &addr, dbgbuf_key_size);
        strncpy((char*)data + dbgbuf_key_size, name, dbgbuf_value_size);
        data[dbgbuf_key_size + dbgbuf_value_size - 1] = 0;

        ++dbgbuf_size;
        vms_shm_dbg_buffer_inc_size(dbgbuf, 1);
        vms_shm_dbg_buffer_bump_version(dbgbuf);
    }
#endif
}

void __tsan_init() {
	/* this one is called for every module and that we do not want.
	 * __vrd_init() is called just once */
}

static void __vrd_init(void) __attribute__((constructor));
void __vrd_init() {
    /* Initialize the info about this source */
    top_control = source_control_define(
        EVENTS_NUM,
	"read", "tl", "write", "tl", "atomicread", "tl",
        "atomicwrite", "tl", "lock", "tl", "unlock", "tl", "alloc", "tll",
        "free", "tl", "fork", "tl", "join", "tl",
	"write_n", "tll", "read_n", "tll");
    if (!top_control) {
	    fprintf(stderr, "Failed creating source control object\n");
	    abort();
    }

    top_shmbuf = create_shared_buffer(shmkey, 512, top_control);
    if (!top_control) {
	    fprintf(stderr, "Failed creating top SHM buffer\n");
	    abort();
    }

    setup_signals();

#ifdef DBGBUF
    dbgbuf = vms_shm_dbg_buffer_create(dbgkey, dbgbuf_capacity, dbgbuf_key_size, dbgbuf_value_size);
#endif

    fprintf(stderr, "info: waiting for the monitor to attach... ");
    buffer_wait_for_monitor(top_shmbuf);
    fprintf(stderr, "done\n");

    fprintf(stderr, "Creating events kinds cache... ");
    size_t num;
    struct event_record *events = buffer_get_avail_events(top_shmbuf, &num);
    assert(num == EVENTS_NUM && "Invalid number of events");
    for (unsigned i = 0; i < EVENTS_NUM; ++i) {
        event_kinds[i] = events[i].kind;
    }
    fprintf(stderr, "done\n");
}

static void __vrd_fini(void) __attribute__((destructor));
void __vrd_fini(void) {
    struct __vrd_thread_data *data, *tmp;
    shm_list_embedded_foreach_safe(data, tmp, &data_list, list) {
        fprintf(stderr, "Thread %lu leaked\n", data->thread_id);
        if (!data->exited) {
            /* XXX: in this case we could get a race... */
            fprintf(stderr, "Thread %lu still running\n", data->thread_id);
            destroy_shared_sub_buffer(data->shmbuf);
        }
        free(data);
    }
}

static inline void *start_event(struct buffer *shm, int type) {
    shm_event *ev;
    while (!(ev = buffer_start_push(shm))) {
        ++thread_data.waited_for_buffer;
    }
    /* push the base info about event */
    ev->id = ++thread_data.last_id;
    ev->kind = event_kinds[type];
    /* push the timestamp */
    uint64_t ts =
        atomic_fetch_add_explicit(&timestamp, 1, memory_order_acq_rel);
    return buffer_partial_push(
        shm,
        (void *)(((unsigned char *)ev) + sizeof(ev->id) + sizeof(ev->kind)),
        &ts, sizeof(ts));
}

void __tsan_func_entry(void *returnaddress) { (void)returnaddress; }
void __tsan_func_exit(void) {}

/*
 * Called before thrd_create.
 *
 * Allocate memory with our data out the thread.
 * Instrumentation replaces the thread data with this memory
 * so that we get this memory on entering the thread
 */
void *__vrd_create_thrd(void *original_data) {
    uint64_t tid = atomic_fetch_add(&last_thread_id, 1);
    /* FIXME: we leak this memory */
    struct __vrd_thread_data *data = malloc(sizeof *data);
    assert(data && "Allocation failed");

    data->data = original_data;
    data->thread_id = tid;
    data->exited = false;
    data->shmbuf = create_shared_sub_buffer(thread_data.shmbuf, 0, top_control);
    if (!data->shmbuf) {
        assert(data->shmbuf && "Failed creating buffer");
        abort();
    }

    shm_list_embedded_insert_after(&data_list, &data->list);

    return data;
}

/* Called after thrd_create. */
void __vrd_thrd_created(void *data, uint64_t std_tid) {
    struct __vrd_thread_data *tdata = (struct __vrd_thread_data *)data;
    struct buffer *shm = thread_data.shmbuf;
    assert(shm && "Do not have SHM buffer");
    void *addr = start_event(shm, EV_FORK);
    buffer_partial_push(shm, addr, &tdata->thread_id, sizeof(tdata->thread_id));
    buffer_finish_push(shm);

    tdata->std_thread_id = std_tid;
}

/* Called at the beginning of the thread routine (or main) */
void *__vrd_thrd_entry(void *data) {
    struct __vrd_thread_data *tdata = (struct __vrd_thread_data *)data;

    thread_data.waited_for_buffer = 0;
    thread_data.last_id = 0;
    thread_data.data = tdata;

    /* assign the SHM buffer to this thread */
    if (data == NULL) {
        thread_data.thread_id = 0;
        thread_data.shmbuf = top_shmbuf;
        return NULL;
    }

    thread_data.thread_id = tdata->thread_id;
    assert(tdata->shmbuf && "Do not have SHM buffer");
    thread_data.shmbuf = tdata->shmbuf;

#ifdef DEBUG_STDOUT
    printf("[%lu] thread %lu started\n", rt_timestamp(), thread_data.thread_id);
#endif
    return tdata->data;
}

void __vrd_thrd_exit(void) {
    struct _thread_data *thr_data = &thread_data;
    struct buffer *shm = thr_data->shmbuf;
    if (thr_data->data) {
        thr_data->data->exited = true;
    }
    /*
    void *addr = start_event(shm, EV_THRD_EXIT);
    buffer_partial_push(shm, addr, &thread_data.thread_id,
    sizeof(thread_data.thread_id)); buffer_finish_push(shm);
    */

    if (shm == top_shmbuf) {
	fprintf(stderr, "info: number of emitted events: %lu\n", timestamp - 1);
        destroy_shared_buffer(shm);
        top_shmbuf = NULL;
#ifdef DBGBUF
	if (dbgbuf) {
	    vms_shm_dbg_buffer_release(dbgbuf);
	    dbgbuf = NULL;
	}
#endif
    } else {
        destroy_shared_sub_buffer(shm);
    }

#ifdef DEBUG_STDOUT
    printf("[%lu] exitting thread %lu\n", rt_timestamp(),
           thread_data.thread_id);
#endif
}

struct __vrd_thread_data *get_data(uint64_t std_tid) {
    struct __vrd_thread_data *data;
    shm_list_embedded_foreach(data, &data_list, list) {
        if (data->std_thread_id == std_tid) {
            return data;
        }
    }
    return NULL;
}

void *__vrd_thrd_join(uint64_t tid) {
    /* we need our thread ID, not the POSIX thread ID. To avoid races and to get
     * the right timestamp, get the associated data with information about the
     * thread that is being joined before calling join() and pass them to
     * __vrd_thrd_joined() that is called after joining. */
    struct __vrd_thread_data *data = get_data(tid);
    if (!data) {
        fprintf(stderr, "ERROR: Found no associated data for thread %lu\n",
                tid);
        return NULL;
    }
    return data;
}

void __vrd_thrd_joined(void *dataptr) {
    struct __vrd_thread_data *data = (struct __vrd_thread_data *)dataptr;
    struct buffer *shm = thread_data.shmbuf;
    void *addr = start_event(shm, EV_JOIN);
    buffer_partial_push(shm, addr, &data->thread_id, sizeof(&data->thread_id));
    buffer_finish_push(shm);

    shm_list_embedded_remove(&data->list);
    free(data);

#ifdef DEBUG_STDOUT
    printf("[%lu] thread %lu joined\n", rt_timestamp(), thread_data.thread_id);
#endif
}

void __tsan_read1(void *addr) {
    struct buffer *shm = thread_data.shmbuf;
    void *mem = start_event(shm, EV_READ);
    buffer_partial_push(shm, mem, &addr, sizeof(addr));
    buffer_finish_push(shm);

#ifdef DEBUG_STDOUT
    printf("[%lu] thread %lu: read1(%p)\n", rt_timestamp(),
           thread_data.thread_id, addr);
#endif
}

void read_N(void *addr, size_t N) {
    struct buffer *shm = thread_data.shmbuf;
    void *mem = start_event(shm, EV_READ_N);
    mem = buffer_partial_push(shm, mem, &addr, sizeof(addr));
    buffer_partial_push(shm, mem, &N, sizeof(N));
    buffer_finish_push(shm);

#ifdef DEBUG_STDOUT
    printf("[%lu] thread %lu: read%lu(%p)\n", rt_timestamp(),
           thread_data.thread_id, N, addr);
#endif
}

void __tsan_read2(void *addr) { read_N(addr, 2); }
void __tsan_read4(void *addr) { read_N(addr, 4); }
void __tsan_read8(void *addr) { read_N(addr, 8); }
void __tsan_read16(void *addr) { read_N(addr, 16); }

void __tsan_write1(void *addr) {
    struct buffer *shm = thread_data.shmbuf;
    void *mem = start_event(shm, EV_WRITE);
    buffer_partial_push(shm, mem, &addr, sizeof(addr));
    buffer_finish_push(shm);

#ifdef DEBUG_STDOUT
    printf("[%lu] thread %lu: write1(%p)\n", rt_timestamp(),
           thread_data.thread_id, addr);
#endif
}

void write_N(void *addr, size_t N) {
    struct buffer *shm = thread_data.shmbuf;
    void *mem = start_event(shm, EV_WRITE_N);
    mem = buffer_partial_push(shm, mem, &addr, sizeof(addr));
    buffer_partial_push(shm, mem, &N, sizeof(N));
    buffer_finish_push(shm);

#ifdef DEBUG_STDOUT
    printf("[%lu] thread %lu: write%lu(%p)\n", rt_timestamp(),
           thread_data.thread_id, N, addr);
#endif
}

void __tsan_write2(void *addr) { write_N(addr, 2); }
void __tsan_write4(void *addr) { write_N(addr, 4); }
void __tsan_write8(void *addr) { write_N(addr, 8); }
void __tsan_write16(void *addr) { write_N(addr, 16); }
void __tsan_unaligned_write16(void *addr) { write_N(addr, 16); }
void __tsan_unaligned_write8(void *addr) { write_N(addr, 8); }
void __tsan_unaligned_write4(void *addr) { write_N(addr, 4); }
void __tsan_unaligned_write2(void *addr) { write_N(addr, 2); }
void __tsan_unaligned_write1(void *addr) { __tsan_write1(addr); }

void __vrd_mutex_lock(void *addr) {
    struct buffer *shm = thread_data.shmbuf;
    void *mem = start_event(shm, EV_LOCK);
    buffer_partial_push(shm, mem, &addr, sizeof(addr));
    buffer_finish_push(shm);

#ifdef DEBUG_STDOUT
    printf("[%lu] thread %lu: mutex_lock(%p)\n", rt_timestamp(),
           thread_data.thread_id, addr);
#endif
}

void __vrd_mutex_unlock(void *addr) {
    struct buffer *shm = thread_data.shmbuf;
    void *mem = start_event(shm, EV_UNLOCK);
    buffer_partial_push(shm, mem, &addr, sizeof(addr));
    buffer_finish_push(shm);

#ifdef DEBUG_STDOUT
    printf("[%lu] thread %lu: mutex_unlock(%p)\n", rt_timestamp(),
           thread_data.thread_id, addr);
#endif
}

int __tsan_atomic32_fetch_add(_Atomic int *x, int val, int memory_order) {
    int tmp = atomic_fetch_add_explicit(x, val, memory_order);
    return tmp;
}

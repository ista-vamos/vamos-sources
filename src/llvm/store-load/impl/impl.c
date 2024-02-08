#include <assert.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>
#include <time.h>


#include "vamos-buffers/core/list-embedded.h"
#include "vamos-buffers/core/source.h"
#include "vamos-buffers/core/utils.h"
#include "vamos-buffers/core/vector-macro.h"
#include "vamos-buffers/shmbuf/buffer.h"
#include "vamos-buffers/shmbuf/client.h"

const char *shmkey = "/vrd";

enum {
    // announce a new watched variable
    EV_VAR = 0,
    EV_VAL = 1,
    EV_LAST = EV_VAL
};

#define EVENTS_NUM (EV_LAST + 1)


/* local cache */
uint64_t event_kinds[EVENTS_NUM];


static CACHELINE_ALIGNED _Atomic size_t last_thread_id = 1;
static CACHELINE_ALIGNED _Atomic size_t timestamp = 1;

static vms_shm_buffer *shmbuf;
static struct vms_source_control *control;
static size_t waited_for_buffer;


static void __vamos_init(void) __attribute__((constructor));
void __vamos_init() {
    /* Initialize the info about this source */
    control = vms_source_control_define(EV_LAST + 1,
                                        "var", "llc",
                                        "val", "ll");
    if (!control) {
        fprintf(stderr, "Failed creating source control object\n");
        abort();
    }

    shmbuf = vms_shm_buffer_create(shmkey, 1024, control);
    if (!shmbuf) {
        fprintf(stderr, "Failed creating top SHM buffer\n");
        abort();
    }

    //setup_signals();

#ifdef DBGBUF
    dbgbuf = vms_shm_dbg_buffer_create(dbgkey, dbgbuf_capacity, dbgbuf_key_size,
                                       dbgbuf_value_size);
#endif

    fprintf(stderr, "info: waiting for the monitor to attach... ");
    vms_shm_buffer_wait_for_reader(shmbuf);
    fprintf(stderr, "done\n");

    // construct the local cache
    size_t num;
    struct vms_event_record *events =
        vms_shm_buffer_get_avail_events(shmbuf, &num);
    assert(num == EVENTS_NUM && "Invalid number of events");
    for (unsigned i = 0; i < EVENTS_NUM; ++i) {
        event_kinds[i] = events[i].kind;
    }
}

static void __vamos_fini(void) __attribute__((destructor));
void __vamos_fini(void) {
    fprintf(stderr, "info: number of emitted events: %lu\n", timestamp - 1);
    vms_shm_buffer_destroy(shmbuf);
    free(control);
}

static inline void *start_event(int type) {
    vms_event *ev;
    while (!(ev = vms_shm_buffer_start_push(shmbuf))) {
        ++waited_for_buffer;
    }
    /* push the base info about event */
    ev->kind = event_kinds[type];
    /* push the ID of the event */
    ev->id = atomic_fetch_add_explicit(&timestamp, 1, memory_order_acq_rel);
    return (void *)(((unsigned char *)ev) + sizeof(ev->id) + sizeof(ev->kind));
}

#define WATCHING_LIMIT 32

static struct watch {
    void *ptr_start;
    void *ptr_end;
} watching[WATCHING_LIMIT];

static unsigned watching_num = 0;

void __vamos_public_input(void *ptr, size_t size) {
    printf("Public input: %p, size: %lu\n", ptr, size);
    unsigned char type = 'i'; // input
    void *addr = start_event(EV_VAR);
    addr = vms_shm_buffer_partial_push(shmbuf, addr, &ptr, sizeof(ptr));
    addr = vms_shm_buffer_partial_push(shmbuf, addr, &size, sizeof(size));
    vms_shm_buffer_partial_push(shmbuf, addr, &type, sizeof(type));
    vms_shm_buffer_finish_push(shmbuf);

    if (watching_num >= WATCHING_LIMIT)
        abort();
    watching[watching_num].ptr_start = ptr;
    watching[watching_num].ptr_end = ptr + size;
    ++watching_num;
}

void __vamos_public_output(void *ptr, size_t size) {
    printf("Public output: %p, size: %lu\n", ptr, size);
    unsigned char type = 'o'; // output
    void *addr = start_event(EV_VAR);
    addr = vms_shm_buffer_partial_push(shmbuf, addr, &ptr, sizeof(ptr));
    addr = vms_shm_buffer_partial_push(shmbuf, addr, &size, sizeof(size));
    vms_shm_buffer_partial_push(shmbuf, addr, &type, sizeof(type));
    vms_shm_buffer_finish_push(shmbuf);

    if (watching_num >= WATCHING_LIMIT)
        abort();
    watching[watching_num].ptr_start = ptr;
    watching[watching_num].ptr_end = ptr + size;
    ++watching_num;
}


int addr_is_watched(void *ptr) {
    for (int i = 0; i < watching_num; ++i) {
        if (watching[i].ptr_start <= ptr && ptr < watching[i].ptr_end)
            return 1;
    }

    return 0;
}

void __vamos_watch_store1(uint8_t val, void *ptr) {
//    printf("Write %u to %p (1 byte)\n", val, ptr);
    if (addr_is_watched(ptr) == 0)
        return;
    void *addr = start_event(EV_VAL);
    uint64_t value = val;
    addr = vms_shm_buffer_partial_push(shmbuf, addr, &ptr, sizeof(ptr));
    vms_shm_buffer_partial_push(shmbuf, addr, &value, sizeof(value));
    vms_shm_buffer_finish_push(shmbuf);
}

void __vamos_watch_store2(uint16_t val, void *ptr) {
 //   printf("Write %u to %p (2 bytes)\n", val, ptr);
    if (addr_is_watched(ptr) == 0)
        return;
    void *addr = start_event(EV_VAL);
    addr = vms_shm_buffer_partial_push(shmbuf, addr, &ptr, sizeof(ptr));
    uint64_t value = val;
    vms_shm_buffer_partial_push(shmbuf, addr, &value, sizeof(value));
    vms_shm_buffer_finish_push(shmbuf);
}

void __vamos_watch_store4(uint32_t val, void *ptr) {
    //  printf("Write %u to %p (4 bytes)\n", val, ptr);
    if (addr_is_watched(ptr) == 0)
        return;
    void *addr = start_event(EV_VAL);
    addr = vms_shm_buffer_partial_push(shmbuf, addr, &ptr, sizeof(ptr));
    uint64_t value = val;
    vms_shm_buffer_partial_push(shmbuf, addr, &value, sizeof(value));
    vms_shm_buffer_finish_push(shmbuf);
}

void __vamos_watch_store8(uint64_t val, void *ptr) {
   // printf("Write %lu to %p (8 bytes)\n", val, ptr);
    if (addr_is_watched(ptr) == 0)
        return;
    void *addr = start_event(EV_VAL);
    addr = vms_shm_buffer_partial_push(shmbuf, addr, &ptr, sizeof(ptr));
    vms_shm_buffer_partial_push(shmbuf, addr, &val, sizeof(val));
    vms_shm_buffer_finish_push(shmbuf);
}

void __vamos_watch_memop(void *ptr, uint64_t len) {
    if (addr_is_watched(ptr) == 0)
        return;

    unsigned char *byte = ptr;
    // 8-bytes pace
    while (len >= sizeof(uint64_t)) {
        void *addr = start_event(EV_VAL);
        addr = vms_shm_buffer_partial_push(shmbuf, addr, &byte, sizeof(void*));
        vms_shm_buffer_partial_push(shmbuf, addr, byte, sizeof(uint64_t));
        vms_shm_buffer_finish_push(shmbuf);

        byte += sizeof(uint64_t);
        len -= sizeof(uint64_t);
    }

    // 4-bytes pace
    while (len >= sizeof(uint32_t)) {
        void *addr = start_event(EV_VAL);
        addr = vms_shm_buffer_partial_push(shmbuf, addr, &byte, sizeof(void*));
        vms_shm_buffer_partial_push(shmbuf, addr, byte, sizeof(uint32_t));
        vms_shm_buffer_finish_push(shmbuf);

        byte += sizeof(uint32_t);
        len -= sizeof(uint32_t);
    }

    // 1-bytes pace (skip 2, I reckon its not that common)
    while (len-- > 0) {
        void *addr = start_event(EV_VAL);
        addr = vms_shm_buffer_partial_push(shmbuf, addr, &byte, sizeof(void*));
        vms_shm_buffer_partial_push(shmbuf, addr, byte, sizeof(unsigned char));
        vms_shm_buffer_finish_push(shmbuf);
        ++byte;
    }

}



#if 0
/* RDTSC */
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

#ifdef DEBUG_STDOUT
#include <stdio.h>
#define PRINT_PREFIX \
    "[\033[37;2m%lu\033[0m] thread \033[36m%lu\033[0m: ts \033[31m%3lu\033[0m"
#endif

#include "vamos-buffers/core/list-embedded.h"
#include "vamos-buffers/core/source.h"
#include "vamos-buffers/core/utils.h"
#include "vamos-buffers/core/vector-macro.h"
#include "vamos-buffers/shmbuf/buffer.h"
#include "vamos-buffers/shmbuf/client.h"

static CACHELINE_ALIGNED _Atomic size_t last_thread_id = 1;
static CACHELINE_ALIGNED _Atomic size_t timestamp = 1;

static vms_shm_buffer *shmbuf;
static struct vms_source_control *control;

#ifdef DEBUG_STDOUT
static inline uint64_t rt_timestamp(void) { return __rdtsc(); }
#endif

const char *shmkey = "/vrd";

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

vms_list_embedded data_list = {&data_list, &data_list};

#ifdef LIST_LOCK_MTX
static mtx_t list_mtx;
static inline void lock() { mtx_lock(&list_mtx); }
static inline bool try_lock() { mtx_try_lock(&list_mtx); }
static inline void unlock() { mtx_unlock(&list_mtx); }
#else
/* An atomic spin lock (well, not exactly spin, we use _mm_pause() to wait
 * a tiny while). Similarly as mtx_t, it is not signal safe. */
static CACHELINE_ALIGNED _Atomic bool __locked = false;

static inline void _lock(_Atomic bool *_lock) {
    while (atomic_exchange_explicit(_lock, true, memory_order_acquire)) {
        _mm_pause();
    }
}

static inline bool _try_lock(_Atomic bool *_lock) {
    return !atomic_exchange_explicit(_lock, true, memory_order_acquire);
}

static inline void _unlock(_Atomic bool *_lock) {
    atomic_store_explicit(_lock, false, memory_order_release);
}

static inline void lock() { _lock(&__locked); }
static inline bool try_lock() { return _try_lock(&__locked); }
static inline void unlock() { _unlock(&__locked); }
#endif /* LIST_LOCK_MTX */

static void (*old_sigabrt_handler)(int);
static void (*old_sigiot_handler)(int);
static void (*old_sigsegv_handler)(int);

static void sig_handler(int sig) {
    printf("signal %d caught...\n", sig);

    struct __vrd_thread_data *data;
    bool print_events_no = false;
    size_t n = 0;
    while (!try_lock()) {
        if (++n > 1000000) {
            /* This might mean that there is only the main thread left
             * and it was interupted inside a locked sequence.
             * Therefore calling lock() would lead to a deadlock.
             * Re-raise the signal and abort this handler to try later
             * (or let the main thread finish) */
            raise(sig);
            return;
        }
    }

    vms_list_embedded_foreach(data, &data_list, list) {
        if (data->shmbuf) {
            vms_shm_buffer_set_destroyed(data->shmbuf);
        }
    }

    if (top_shmbuf) {
        /* This is not atomic, but it works in most cases which is enough for
         * us. */
        print_events_no = true;
        vms_shm_buffer_set_destroyed(top_shmbuf);
        top_shmbuf = NULL;
    }

#ifdef DBGBUF
    if (dbgbuf) {
        vms_shm_dbg_buffer_release(dbgbuf);
        dbgbuf = NULL;
    }
#endif
    unlock();

    /* restore previous handlers -- threads now can fail on assertions
       about using destroyed buffers, but we do not care anymore.
       Buffers are disconnected and the program would be killed anyway. */
    signal(SIGABRT, old_sigabrt_handler);
    signal(SIGIOT, old_sigiot_handler);
    signal(SIGSEGV, old_sigsegv_handler);

    if (print_events_no) {
        fprintf(stderr, "info: number of emitted events: %lu\n", timestamp - 1);
    }
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

        unsigned char *data =
            vms_shm_dbg_buffer_data(dbgbuf) +
            dbgbuf_size * (dbgbuf_key_size + dbgbuf_value_size);
        memcpy((char *)data, &addr, dbgbuf_key_size);
        strncpy((char *)data + dbgbuf_key_size, name, dbgbuf_value_size);
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
#ifdef LIST_LOCK_MTX
    mtx_init(&list_mtx, mtx_plain);
#endif
    /* Initialize the info about this source */
    top_control = vms_source_control_define(
        EVENTS_NUM, "read", "tl", "write", "tl", "atomicread", "tl",
        "atomicwrite", "tl", "lock", "tl", "unlock", "tl", "alloc", "tll",
        "free", "tl", "fork", "tl", "join", "tl", "write_n", "tll", "read_n",
        "tll");
    if (!top_control) {
        fprintf(stderr, "Failed creating source control object\n");
        abort();
    }

    top_shmbuf = vms_shm_buffer_create(shmkey, 598, top_control);
    if (!top_shmbuf) {
        fprintf(stderr, "Failed creating top SHM buffer\n");
        abort();
    }

    setup_signals();

#ifdef DBGBUF
    dbgbuf = vms_shm_dbg_buffer_create(dbgkey, dbgbuf_capacity, dbgbuf_key_size,
                                       dbgbuf_value_size);
#endif

    fprintf(stderr, "info: waiting for the monitor to attach... ");
    vms_shm_buffer_wait_for_reader(top_shmbuf);
    fprintf(stderr, "done\n");

    size_t num;
    struct vms_event_record *events =
        vms_shm_buffer_get_avail_events(top_shmbuf, &num);
    assert(num == EVENTS_NUM && "Invalid number of events");
    for (unsigned i = 0; i < EVENTS_NUM; ++i) {
        event_kinds[i] = events[i].kind;
    }
}

static void __vrd_fini(void) __attribute__((destructor));
void __vrd_fini(void) {
    struct __vrd_thread_data *data, *tmp;
    VEC(leaked_threads, size_t);
    VEC_INIT(leaked_threads);
    VEC(running_threads, size_t);
    VEC_INIT(running_threads);

    lock();
    vms_list_embedded_foreach_safe(data, tmp, &data_list, list) {
        vms_list_embedded_remove(&data->list);

        VEC_PUSH(leaked_threads, &data->thread_id);

        if (data->shmbuf) {
            VEC_PUSH(running_threads, &data->thread_id);
            vms_shm_buffer_destroy_sub_buffer(data->shmbuf);
            data->shmbuf = NULL;
        }
        free(data);
    }
    unlock();

    bool print_events_no = false;
    lock();
    if (top_shmbuf) {
        print_events_no = true;
        vms_shm_buffer_destroy(top_shmbuf);

        assert(thread_data.shmbuf == top_shmbuf);
        thread_data.shmbuf = NULL;
        top_shmbuf = NULL;
    }
    unlock();

    if (print_events_no) {
        fprintf(stderr, "info: number of emitted events: %lu\n", timestamp - 1);
    }
    for (unsigned i = 0; i < VEC_SIZE(leaked_threads); ++i) {
        fprintf(stderr, "[vamos] warning: thread %lu leaked\n",
                leaked_threads[i]);
    }
    for (unsigned i = 0; i < VEC_SIZE(running_threads); ++i) {
        fprintf(stderr,
                "[vamos] warning: thread %lu is still running (expect crash)\n",
                running_threads[i]);
    }
#ifdef DBGBUF
    if (dbgbuf) {
        vms_shm_dbg_buffer_release(dbgbuf);
        dbgbuf = NULL;
    }
#endif
}

static inline void *start_event(vms_shm_buffer *shm, int type) {
    vms_event *ev;
    while (!(ev = vms_shm_buffer_start_push(shm))) {
        ++thread_data.waited_for_buffer;
    }
    /* push the base info about event */
    ev->id = ++thread_data.last_id;
    ev->kind = event_kinds[type];
    /* push the timestamp */
    uint64_t ts =
        atomic_fetch_add_explicit(&timestamp, 1, memory_order_acq_rel);
    return vms_shm_buffer_partial_push(
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
void *__vrd_create_thrd(void *original_fun, void *original_data) {
    uint64_t tid = atomic_fetch_add(&last_thread_id, 1);
    struct __vrd_thread_data *data = malloc(sizeof *data);
    assert(data && "Allocation failed");

    assert(tid > 0 && "invalid ID");
    data->orig_data = original_data;
    data->orig_fun = original_fun;
    data->thread_id = tid;
    data->exited = false;
    data->wait_for_parent = 0;
    data->shmbuf =
        vms_shm_buffer_create_sub_buffer(thread_data.shmbuf, 0, top_control);
    if (!data->shmbuf) {
        assert(data->shmbuf && "Failed creating buffer");
        abort();
    }

    lock();
    vms_list_embedded_insert_after(&data_list, &data->list);
    unlock();

    return data;
}

/* Called after thrd_create. */
void __vrd_thrd_created(void *data, uint64_t std_tid) {
    struct __vrd_thread_data *tdata = (struct __vrd_thread_data *)data;
    vms_shm_buffer *shm = thread_data.shmbuf;
    assert(shm && "Do not have SHM buffer");
    void *addr = start_event(shm, EV_FORK);
    vms_shm_buffer_partial_push(shm, addr, &tdata->thread_id,
                                sizeof(tdata->thread_id));
#ifdef DEBUG_STDOUT
    size_t ts = *(size_t *)(((unsigned char *)addr) - sizeof(size_t));
#endif
    vms_shm_buffer_finish_push(shm);

    /* notify the thread that it can proceed
       (we cannot allow the thread to emit any events before the fork event is
       sent and the fork event must be sent from here because we need the thread
       ID)
    */
    atomic_store_explicit(&tdata->wait_for_parent, 1, memory_order_release);

    tdata->std_thread_id = std_tid;
    assert(tdata->thread_id > 0 && "invalid ID");

#ifdef DEBUG_STDOUT
    fprintf(stderr, PRINT_PREFIX " created thread %lu\n", rt_timestamp(),
            thread_data.thread_id, ts, tdata->thread_id);
#endif
}

static void setup_thread(struct __vrd_thread_data *tdata) {
    assert(tdata != NULL);

    thread_data.waited_for_buffer = 0;
    thread_data.last_id = 0;
    thread_data.data = tdata;
    thread_data.thread_id = tdata->thread_id;
    thread_data.shmbuf = tdata->shmbuf;

    while (
        !atomic_load_explicit(&tdata->wait_for_parent, memory_order_acquire)) {
        /* wait until the parent thread sends the EV_FORK event
         * which must occur before any event in this thread */
        _mm_pause();
    }
}

static void tear_down_thread(struct __vrd_thread_data *tdata) {
    lock();
    if (tdata->shmbuf) {
        vms_shm_buffer_destroy_sub_buffer(tdata->shmbuf);
        tdata->shmbuf = NULL;
    }
    unlock();
}

/* Called at the beginning of the thread routine (or main) */
void *__vrd_run_thread(void *data) {
    struct __vrd_thread_data *tdata = (struct __vrd_thread_data *)data;

    setup_thread(tdata);
    /* run the thread */
    void *(*fun)(void *) = tdata->orig_fun;
    void *ret = fun(tdata->orig_data);
    tear_down_thread(tdata);
    return ret;
}

int __vrd_run_thread_c11(void *data) {
    struct __vrd_thread_data *tdata = (struct __vrd_thread_data *)data;

    setup_thread(tdata);
    /* run the thread */
    int (*fun)(void *) = tdata->orig_fun;
    int ret = fun(tdata->orig_data);
    tear_down_thread(tdata);
    return ret;
}

void __vrd_thrd_exit(void) { tear_down_thread(thread_data.data); }

void __vrd_setup_main_thread(void) {
    thread_data.waited_for_buffer = 0;
    thread_data.last_id = 0;
    thread_data.data = NULL;
    thread_data.thread_id = 0;
    thread_data.shmbuf = top_shmbuf;
}

void __vrd_exit_main_thread(void) {
    fprintf(stderr, "info: number of emitted events: %lu\n", timestamp - 1);
    lock();
    if (top_shmbuf) {
        vms_shm_buffer_destroy(top_shmbuf);
        top_shmbuf = NULL;
    }
#ifdef DBGBUF
    if (dbgbuf) {
        vms_shm_dbg_buffer_release(dbgbuf);
        dbgbuf = NULL;
    }
#endif
    unlock();
}

static inline struct __vrd_thread_data *_get_data(uint64_t std_tid) {
    struct __vrd_thread_data *data;
    vms_list_embedded_foreach(data, &data_list, list) {
        if (data->std_thread_id == std_tid) {
            return data;
        }
    }
    return NULL;
}

struct __vrd_thread_data *get_data(uint64_t std_tid) {
    lock();
    struct __vrd_thread_data *data = _get_data(std_tid);
    unlock();
    return data;
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
    vms_shm_buffer *shm = thread_data.shmbuf;
    void *addr = start_event(shm, EV_JOIN);
#ifdef DEBUG_STDOUT
    size_t ts = *(size_t *)(((unsigned char *)addr) - sizeof(size_t));
    size_t tid = data->thread_id;
#endif
    vms_shm_buffer_partial_push(shm, addr, &data->thread_id,
                                sizeof(&data->thread_id));
    vms_shm_buffer_finish_push(shm);

    lock();
    vms_list_embedded_remove(&data->list);
    unlock();
    free(data);

#ifdef DEBUG_STDOUT
    fprintf(stderr, PRINT_PREFIX " joined %lu\n", rt_timestamp(),
            thread_data.thread_id, ts, tid);
#endif
}

void __tsan_read1(void *addr) {
    vms_shm_buffer *shm = thread_data.shmbuf;
    void *mem = start_event(shm, EV_READ);
#ifdef DEBUG_STDOUT
    size_t ts = *(size_t *)(((unsigned char *)mem) - sizeof(size_t));
#endif
    vms_shm_buffer_partial_push(shm, mem, &addr, sizeof(addr));
    vms_shm_buffer_finish_push(shm);

#ifdef DEBUG_STDOUT
    fprintf(stderr, PRINT_PREFIX " read1(%p)\n", rt_timestamp(),
            thread_data.thread_id, ts, addr);
#endif
}

void read_N(void *addr, size_t N) {
    vms_shm_buffer *shm = thread_data.shmbuf;
    void *mem = start_event(shm, EV_READ_N);
#ifdef DEBUG_STDOUT
    size_t ts = *(size_t *)(((unsigned char *)mem) - sizeof(size_t));
#endif
    mem = vms_shm_buffer_partial_push(shm, mem, &addr, sizeof(addr));
    vms_shm_buffer_partial_push(shm, mem, &N, sizeof(N));
    vms_shm_buffer_finish_push(shm);

#ifdef DEBUG_STDOUT
    fprintf(stderr, PRINT_PREFIX " read%lu(%p)\n", rt_timestamp(),
            thread_data.thread_id, ts, N, addr);
#endif
}

void __tsan_read2(void *addr) { read_N(addr, 2); }
void __tsan_read4(void *addr) { read_N(addr, 4); }
void __tsan_read8(void *addr) { read_N(addr, 8); }
void __tsan_read16(void *addr) { read_N(addr, 16); }
void __tsan_unaligned_read16(void *addr) { read_N(addr, 16); }
void __tsan_unaligned_read8(void *addr) { read_N(addr, 8); }
void __tsan_unaligned_read4(void *addr) { read_N(addr, 4); }
void __tsan_unaligned_read2(void *addr) { read_N(addr, 2); }
void __tsan_unaligned_read1(void *addr) { __tsan_read1(addr); }

void __tsan_write1(void *addr) {
    vms_shm_buffer *shm = thread_data.shmbuf;
    void *mem = start_event(shm, EV_WRITE);
#ifdef DEBUG_STDOUT
    size_t ts = *(size_t *)(((unsigned char *)mem) - sizeof(size_t));
#endif
    vms_shm_buffer_partial_push(shm, mem, &addr, sizeof(addr));
    vms_shm_buffer_finish_push(shm);

#ifdef DEBUG_STDOUT
    fprintf(stderr, PRINT_PREFIX " write1(%p)\n", rt_timestamp(),
            thread_data.thread_id, ts, addr);
#endif
}

void write_N(void *addr, size_t N) {
    vms_shm_buffer *shm = thread_data.shmbuf;
    void *mem = start_event(shm, EV_WRITE_N);
#ifdef DEBUG_STDOUT
    size_t ts = *(size_t *)(((unsigned char *)mem) - sizeof(size_t));
#endif
    mem = vms_shm_buffer_partial_push(shm, mem, &addr, sizeof(addr));
    vms_shm_buffer_partial_push(shm, mem, &N, sizeof(N));
    vms_shm_buffer_finish_push(shm);

#ifdef DEBUG_STDOUT
    fprintf(stderr, PRINT_PREFIX " write%lu(%p)\n", rt_timestamp(),
            thread_data.thread_id, ts, N, addr);
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
void *__tsan_memset(void *addr, char c, size_t n) {
    write_N(addr, n);
    return memset(addr, c, n);
}

void __vrd_mutex_lock(void *addr) {
    vms_shm_buffer *shm = thread_data.shmbuf;
    void *mem = start_event(shm, EV_LOCK);
#ifdef DEBUG_STDOUT
    size_t ts = *(size_t *)(((unsigned char *)mem) - sizeof(size_t));
#endif
    vms_shm_buffer_partial_push(shm, mem, &addr, sizeof(addr));
    vms_shm_buffer_finish_push(shm);

#ifdef DEBUG_STDOUT
    fprintf(stderr, PRINT_PREFIX " mutex_lock(%p)\n", rt_timestamp(),
            thread_data.thread_id, ts, addr);
#endif
}

void __vrd_mutex_unlock(void *addr) {
    vms_shm_buffer *shm = thread_data.shmbuf;
    void *mem = start_event(shm, EV_UNLOCK);
#ifdef DEBUG_STDOUT
    size_t ts = *(size_t *)(((unsigned char *)mem) - sizeof(size_t));
#endif
    vms_shm_buffer_partial_push(shm, mem, &addr, sizeof(addr));
    vms_shm_buffer_finish_push(shm);

#ifdef DEBUG_STDOUT
    fprintf(stderr, PRINT_PREFIX " mutex_unlock(%p)\n", rt_timestamp(),
            thread_data.thread_id, ts, addr);
#endif
}

int __tsan_atomic32_fetch_add(_Atomic int *x, int val, int memory_order) {
    int tmp = atomic_fetch_add_explicit(x, val, memory_order);
    return tmp;
}
#endif

/*
 * Based on Code Manipulation API Sample (syscall.c) from DynamoRIO
 */

#include <assert.h>
#include <regex.h>
#include <string.h> /* memset */

#include "dr_api.h"
#include "drmgr.h"

#include "buffer.h"
#include "client.h"
#include "event.h"
#include "signatures.h"
#include "source.h"
#include "streams/stream-drregex.h" /* event type */

#include "../fastbuf/shm_monitored.h"

#ifdef UNIX
#if defined(MACOS) || defined(ANDROID)
#include <sys/syscall.h>
#else
#include <syscall.h>
#endif
#endif

/* Some syscalls have more args, but this is the max we need for
 * SYS_write/NtWriteFile */
#ifdef WINDOWS
#define SYS_MAX_ARGS 9
#else
#define SYS_MAX_ARGS 3
#endif

#ifndef WITH_STDOUT
//#define dr_printf(...) do{}while(0)
#endif

#define MAXMATCH 20

typedef struct {
    int fd;
    void *buf;
    size_t size;
    size_t thread;
} per_thread_t;

/* Thread-context-local storage index from drmgr */
static int tcls_idx;
/* we'll number threads from 0 up */
static size_t thread_num = 0;

static struct buffer *shm;
/* shmbuf assumes one writer and one reader, but here we may have multiple
 writers
 * (multiple threads), so we must make sure they are seuqntialized somehow
   (until we have the implementation for multiple-writers) */
static size_t waiting_for_buffer = 0;
static _Atomic(bool) _write_lock = false;

static struct event_record *events;
static size_t events_num;

static inline void write_lock() {
    _Atomic bool *l = &_write_lock;
    bool unlocked;
    do {
        unlocked = false;
    } while (atomic_compare_exchange_weak(l, &unlocked, true));
}

static inline void write_unlock() {
    /* FIXME: use explicit memory ordering, seq_cnt is not needed here */
    _write_lock = false;
}

/* The system call number of SYS_write/NtWriteFile */
static int write_sysnum, read_sysnum;

/* for some reason we need this...*/
#undef bool
#define bool char

static int get_write_sysnum(void);
static int get_read_sysnum(void);
static void event_exit(void);
static bool event_filter_syscall(void *drcontext, int sysnum);
static bool event_pre_syscall(void *drcontext, int sysnum);
static void event_post_syscall(void *drcontext, int sysnum);
static void event_thread_context_init(void *drcontext, bool new_depth);
static void event_thread_context_exit(void *drcontext, bool process_exit);

static void usage_and_exit(int ret) {
    dr_fprintf(
        STDERR,
        "Usage: drrun shmkey name expr sig [name expr sig] ... -- program\n");
    exit(ret);
}

static char **signatures;
static regex_t *re;
static size_t exprs_num;
shm_event_drregex ev;

static char *tmpline = NULL;
static size_t tmpline_len = 0;
static char *partial_line = 0;
static size_t partial_line_len = 0;
static size_t partial_line_alloc_len = 0;

static void parse_line(bool iswrite, per_thread_t *data, char *line) {
#ifdef DRREGEX_ONLY_ARGS
    (void)data;
    (void)iswrite;
#endif
    int status;
    signature_operand op;
    ssize_t len;
    regmatch_t matches[MAXMATCH + 1];

    /* fprintf(stderr, "LINE: %s\n", line); */

    for (int i = 0; i < (int)exprs_num; ++i) {
        if (events[i].kind == 0)
            continue; /* monitor is not interested in this */

        status = regexec(&re[i], line, MAXMATCH, matches, 0);
        if (status != 0) {
            continue;
        }
        int m = 1;
        void *addr;

        /** LOCKED --
         * FIXME: we hold the lock long, first create the event locally and only
         * then push it **/
        write_lock();

        while (!(addr = buffer_start_push(shm))) {
            ++waiting_for_buffer;
        }
        /* push the base info about event */
        ++ev.base.id;
        ev.base.kind = events[i].kind;
#ifndef DRREGEX_ONLY_ARGS
        ev.write = iswrite;
        ev.fd = data->fd;
        ev.thread = data->thread;
#endif
        addr = buffer_partial_push(shm, addr, &ev, sizeof(ev));

        /* push the arguments of the event */
        for (const char *o = signatures[i]; *o && m <= MAXMATCH; ++o, ++m) {
            if (*o == 'L') { /* user wants the whole line */
                addr = buffer_partial_push_str(shm, addr, ev.base.id, line);
                continue;
            }
            if (*o != 'M') {
                if ((int)matches[m].rm_so < 0) {
                    dr_fprintf(
                        STDERR,
                        "warning: have no match for '%c' in signature %s\n", *o,
                        signatures[i]);
                    continue;
                }
                len = matches[m].rm_eo - matches[m].rm_so;
            } else {
                len = matches[0].rm_eo - matches[0].rm_so;
            }

            /* make sure we have big enough temporary buffer */
            if (tmpline_len < (size_t)len) {
                free(tmpline);
                tmpline = malloc(sizeof(char) * len + 1);
                assert(tmpline && "Memory allocation failed");
                tmpline_len = len;
            }

            if (*o == 'M') { /* user wants the whole match */
                assert(matches[0].rm_so >= 0);
                strncpy(tmpline, line + matches[0].rm_so, len);
                tmpline[len] = '\0';
                addr = buffer_partial_push_str(shm, addr, ev.base.id, tmpline);
                continue;
            } else {
                strncpy(tmpline, line + matches[m].rm_so, len);
                tmpline[len] = '\0';
            }

            switch (*o) {
            case 'c':
                assert(len == 1);
                addr = buffer_partial_push(
                    shm, addr, (char *)(line + matches[m].rm_eo), sizeof(op.c));
                break;
            case 'i':
                op.i = atoi(tmpline);
                addr = buffer_partial_push(shm, addr, &op.i, sizeof(op.i));
                break;
            case 'l':
                op.l = atol(tmpline);
                addr = buffer_partial_push(shm, addr, &op.l, sizeof(op.l));
                break;
            case 'f':
                op.f = atof(tmpline);
                addr = buffer_partial_push(shm, addr, &op.f, sizeof(op.f));
                break;
            case 'd':
                op.d = strtod(tmpline, NULL);
                addr = buffer_partial_push(shm, addr, &op.d, sizeof(op.d));
                break;
            case 'S':
                addr = buffer_partial_push_str(shm, addr, ev.base.id, tmpline);
                break;
            default:
                assert(0 && "Invalid signature");
            }
        }
        buffer_finish_push(shm);
        write_unlock();
    }
}

static void push_event(bool iswrite, per_thread_t *data, ssize_t retlen) {
    /*
    dr_fprintf(STDERR,
               "%s fd %d: buf-size: %lu, retlen: %lu, buf: \"%.*s\"\n",
               iswrite ? "WRITE" : "READ", data->fd, data->size, retlen, retlen,
    data->buf);
               */

    char *line = data->buf;
    const char *endptr = line + retlen;
    char *line_end = line;
    do {
        while (line_end != endptr && *line_end != '\n') {
            ++line_end;
        }

        if (line_end == endptr) {
            const size_t linelen = endptr - line;
            if (partial_line_len > 0) {
                if (partial_line_alloc_len <= partial_line_len + linelen) {
                    partial_line_alloc_len = partial_line_len + linelen + 1;
                    partial_line =
                        realloc(partial_line, partial_line_alloc_len);
                    DR_ASSERT(partial_line && "Allocation failed");
                }
                strncpy(partial_line + partial_line_len, line, linelen);
                partial_line_len += linelen;
            } else {
                if (partial_line_alloc_len <= linelen) {
                    free(partial_line);
                    partial_line_alloc_len = linelen + 1;
                    partial_line = malloc(partial_line_alloc_len);
                    DR_ASSERT(partial_line && "Allocation failed");
                }
                DR_ASSERT(partial_line_alloc_len > linelen);
                strncpy(partial_line, line, linelen);
                partial_line_len = linelen;
            }
            break;
        }

        if (partial_line_len > 0) {
            const size_t linelen = line_end - line;
            if (partial_line_alloc_len <= partial_line_len + linelen) {
                partial_line_alloc_len = partial_line_len + linelen + 1;
                partial_line = realloc(partial_line, partial_line_alloc_len);
                DR_ASSERT(partial_line && "Allocation failed");
            }
            strncpy(partial_line + partial_line_len, line, linelen);
            partial_line_len += linelen;
            partial_line[partial_line_len] = 0;

            parse_line(iswrite, data, partial_line);
            partial_line_len = 0;
        } else {

            DR_ASSERT(*line_end == '\n');
            *line_end = 0; /* temporary end of line */
            parse_line(iswrite, data, line);
            *line_end = '\n';
        }

        line = ++line_end;
        if (line == endptr)
            break; /* all done */
    } while (1);
}

DR_EXPORT void dr_client_main(client_id_t id, int argc, const char *argv[]) {
    (void)id;
    dr_set_client_name("Shamon intercept write and read syscalls",
                       "http://...");
    drmgr_init();
    write_sysnum = get_write_sysnum();
    read_sysnum = get_read_sysnum();
    dr_register_filter_syscall_event(event_filter_syscall);
    drmgr_register_pre_syscall_event(event_pre_syscall);
    drmgr_register_post_syscall_event(event_post_syscall);
    dr_register_exit_event(event_exit);
    tcls_idx = drmgr_register_cls_field(event_thread_context_init,
                                        event_thread_context_exit);
    DR_ASSERT(tcls_idx != -1);
    if (dr_is_notify_on()) {
#ifdef WINDOWS
        /* ask for best-effort printing to cmd window.  must be called at init.
         */
        dr_enable_console_printing();
#endif
        dr_fprintf(STDERR, "Client DrRegex is running\n");
    }
    if (argc < 5 && (argc - 2) % 3 != 0) {
        usage_and_exit(1);
    }

    exprs_num = (argc - 1) / 3;
    if (exprs_num == 0) {
        usage_and_exit(1);
    }

    const char *shmkey = argv[1];
    char *exprs[exprs_num];
    char *names[exprs_num];
    signatures = dr_global_alloc(exprs_num * sizeof(char *));
    re = dr_global_alloc(exprs_num * sizeof(regex_t));

    int arg_i = 2;
    for (int i = 0; i < (int)exprs_num; ++i) {
        names[i] = (char *)argv[arg_i++];
        exprs[i] = (char *)argv[arg_i++];
        if (arg_i >= argc) {
            dr_fprintf(STDERR, "Missing a signature for '%s'\n", exprs[i]);
            usage_and_exit(1);
        }
        signatures[i] = (char *)argv[arg_i++];

        /* compile the regex, use extended RE */
        int status = regcomp(&re[i], exprs[i], REG_EXTENDED);
        if (status != 0) {
            dr_fprintf(STDERR, "Failed compiling regex '%s'\n", exprs[i]);
            /* FIXME: we leak the expressions compiled so far ... */
            exit(1);
        }
    }

    /* Initialize the info about this source */
    struct source_control *control = source_control_define_pairwise(
        exprs_num, (const char **)names, (const char **)signatures);
    assert(control);

    shm = create_shared_buffer(shmkey, control);
    assert(shm);
    events = buffer_get_avail_events(shm, &events_num);
    free(control);

    dr_fprintf(STDERR, "info: waiting for the monitor to attach\n");
    buffer_wait_for_monitor(shm);
}

static void event_exit(void) {
    if (!drmgr_unregister_cls_field(event_thread_context_init,
                                    event_thread_context_exit, tcls_idx) ||
        !drmgr_unregister_pre_syscall_event(event_pre_syscall) ||
        !drmgr_unregister_post_syscall_event(event_post_syscall))
        DR_ASSERT(false && "failed to unregister");
    drmgr_exit();
    dr_fprintf(STDERR,
               "info: sent %lu events, busy waited on buffer %lu cycles\n",
               ev.base.id, waiting_for_buffer);
    for (int i = 0; i < (int)exprs_num; ++i) {
        regfree(&re[i]);
    }

    free(tmpline);
    free(partial_line);

    dr_printf("Destroying shared buffer\n");
    destroy_shared_buffer(shm);
}

static void event_thread_context_init(void *drcontext, bool new_depth) {
    /* create an instance of our data structure for this thread context */
    per_thread_t *data;
    if (new_depth) {
        data = (per_thread_t *)dr_thread_alloc(drcontext, sizeof(per_thread_t));
        drmgr_set_cls_field(drcontext, tcls_idx, data);
        data->fd = -1;
        data->thread = thread_num++;
        // FIXME: typo in the name
        // intialize_thread_buffer(1, 2);
    } else {
        data = (per_thread_t *)drmgr_get_cls_field(drcontext, tcls_idx);
    }
}

static void event_thread_context_exit(void *drcontext, bool thread_exit) {
    if (!thread_exit)
        return;
    per_thread_t *data =
        (per_thread_t *)drmgr_get_cls_field(drcontext, tcls_idx);
    dr_thread_free(drcontext, data, sizeof(per_thread_t));
    // close_thread_buffer();
}

static bool event_filter_syscall(void *drcontext, int sysnum) {
    (void)drcontext;
    return sysnum == write_sysnum || sysnum == read_sysnum;
}

static bool event_pre_syscall(void *drcontext, int sysnum) {
    /* do we need this check?  we have the filter... */
    if (sysnum != read_sysnum && sysnum != write_sysnum) {
        return true;
    }

    reg_t fd = dr_syscall_get_param(drcontext, 0);
    reg_t buf = dr_syscall_get_param(drcontext, 1);
    reg_t size = dr_syscall_get_param(drcontext, 2);
    per_thread_t *data =
        (per_thread_t *)drmgr_get_cls_field(drcontext, tcls_idx);
    data->fd = fd; /* store the fd for post-event */
    data->buf = (void *)buf;
    data->size = size;
    return true; /* execute normally */
}

static void event_post_syscall(void *drcontext, int sysnum) {
    reg_t retval = dr_syscall_get_result(drcontext);

    if (sysnum != read_sysnum && sysnum != write_sysnum) {
        return;
    }
    per_thread_t *data =
        (per_thread_t *)drmgr_get_cls_field(drcontext, tcls_idx);
    /*
    if(data->fd>2)
    {
        return;
    }
    */
    /* right now we can handle just *one* filedescriptor (we have just one
     * buffer for incomplete lines), so use stdout */
    if (data->fd != 1) {
        return;
    }
    ssize_t len = *((ssize_t *)&retval);
    // dr_printf("Syscall: %i; len: %li; result: %lu\n",sysnum, len, len);
    push_event(sysnum == write_sysnum, data, len);
}

static int get_write_sysnum(void) {
    /* XXX: we could use the "drsyscall" Extension from the Dr. Memory Framework
     * (DRMF) to obtain the number of any system call from the name.
     */
#ifdef UNIX
    return SYS_write;
#else
    byte *entry;
    module_data_t *data = dr_lookup_module_by_name("ntdll.dll");
    DR_ASSERT(data != NULL);
    entry = (byte *)dr_get_proc_address(data->handle, "NtWriteFile");
    DR_ASSERT(entry != NULL);
    dr_free_module_data(data);
    return drmgr_decode_sysnum_from_wrapper(entry);
#endif
}

static int get_read_sysnum(void) {
#ifdef UNIX
    return SYS_read;
#else
    byte *entry;
    module_data_t *data = dr_lookup_module_by_name("ntdll.dll");
    DR_ASSERT(data != NULL);
    entry = (byte *)dr_get_proc_address(data->handle, "NtReadFile");
    DR_ASSERT(entry != NULL);
    dr_free_module_data(data);
    return drmgr_decode_sysnum_from_wrapper(entry);
#endif
}

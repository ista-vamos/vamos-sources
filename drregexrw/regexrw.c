/*
 * Based on Code Manipulation API Sample (syscall.c) from DynamoRIO
 */

#include <assert.h>
#include <regex.h>
#include <string.h> /* memset */
#include <stdatomic.h>
#include <alloca.h>

#include "dr_api.h"
#include "drmgr.h"

#include "buffer.h"
#include "client.h"
#include "event.h"
#include "signatures.h"
#include "source.h"
#include "streams/stream-drregex.h" /* event type */

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

// static struct buffer *shm;
/* shmbuf assumes one writer and one reader, but here we may have multiple
 writers
 * (multiple threads), so we must make sure they are seuqntialized somehow
   (until we have the implementation for multiple-writers) */
static size_t waiting_for_buffer = 0;
static _Atomic(bool) _write_lock = false;

// static struct event_record *events;
// static size_t events_num;

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
        "Usage: drrun -c libdrregexrw.so shmkey outsplit name expr sig [name expr sig] ... -/- insplit name expr sig [name expr sig] ...\n");
    exit(ret);
}

struct event {
    shm_event base;
#ifdef WITH_LINES
    size_t line;
#endif
    unsigned char args[];
};

typedef struct _parsedata
{
    size_t exprs_num;
    const char** exprs;
    const char **signatures;
    const char **names;
    struct buffer *shm;
    struct event_record *events;
    struct event ev;
    const char* delim;
    regex_t re[];
} parsedata;
parsedata* pd_out;
parsedata* pd_in;

//shm_event_drregex ev;

// static char *tmpline = NULL;
// static size_t tmpline_len = 0;
// static char *partial_line = 0;
// static size_t partial_line_len = 0;
// static size_t partial_line_alloc_len = 0;

typedef struct msgbuf
{
	struct msgbuf *next;
	struct msgbuf *prev;
	char *textbuf;
	size_t offset;
} msgbuf;

msgbuf outbuf;
msgbuf inbuf;

void insert_message(msgbuf *buf, char *textbuf, ssize_t slen)
{
    size_t len=0;
    if(slen>0)
    {
        len=slen;
    }
    else
    {
        return;
    }
    char* text=(char*)malloc(sizeof(char)*(len+1));
    strncpy(text, textbuf, len);
    text[len]=0;
	if (buf->textbuf == NULL)
	{
		buf->textbuf = text;
		buf->offset = sizeof(size_t)+sizeof(int64_t);
	}
	else
	{
		msgbuf *newbuf = (msgbuf *)malloc(sizeof(msgbuf));
		newbuf->textbuf = text;
		newbuf->offset=sizeof(size_t) + sizeof(int64_t);
		newbuf->next = buf;
		newbuf->prev = buf->prev;
		newbuf->next->prev = newbuf;
		newbuf->prev->next = newbuf;
	}
}

char *buf_get_upto(msgbuf *buf, const char* delim)
{
	msgbuf *current = buf;
	size_t size = 0;
	if (current->textbuf == NULL)
	{
		return NULL;
	}
	do
	{
		char *nlpos = strstr(current->textbuf, delim);
		if (nlpos != NULL)
		{
			size_t len = (((intptr_t)nlpos) - ((intptr_t)current->textbuf)) + strlen(delim);
			char *ret = (char *)malloc(size + len + 1);
			memcpy(ret + size, current->textbuf, len);
			ret[size + len] = 0;
			size_t curlen = strlen(current->textbuf);
			if (curlen == len)
			{
				free(current->textbuf - current->offset);
				current->textbuf = NULL;
				if (current == buf)
				{
					if (current->next != current)
					{
						current->textbuf = current->next->textbuf;
						current->offset = current->next->offset;
						msgbuf *newnext = current->next->next;
						newnext->prev = current;
						free(current->next);
						current->next = newnext;
					}
					return ret;
				}
                else
                {
				    current->next->prev = current->prev;
				    current->prev->next = current->next;
                }
				msgbuf *prev = current->prev;
				free(current);
				current = prev;
			}
			else
			{
				current->textbuf+=len;
				current->offset+=len;
				if (current == buf)
				{
					return ret;
				}
				current = current->prev;
			}
			while (1)
			{
				size_t curlen = strlen(current->textbuf);
				size -= curlen;
				memcpy(ret + size, current->textbuf, curlen);
				free(current->textbuf - current->offset);
				current->textbuf = NULL;
				if (current == buf)
				{
					if (current->next != current)
					{
						current->textbuf = current->next->textbuf;
						current->offset = current->next->offset;
						msgbuf *newnext = current->next->next;
						newnext->prev = current;
						free(current->next);
						current->next = newnext;
					}
					return ret;
				}
                else
                {
				    current->next->prev = current->prev;
				    current->prev->next = current->next;
                }
				msgbuf *prev = current->prev;
				free(current);
				current = prev;
			}
		}
		size += strlen(current->textbuf);
		current = current->next;
	} while (current != buf);
	return NULL;
}

char *buf_get_line(msgbuf *buf)
{
	return buf_get_upto(buf, "\n");
}

static inline void process_messages(msgbuf * buf, parsedata * const pd) {
    regmatch_t matches[MAXMATCH+1];
    const char** signatures = pd->signatures;
    struct event_record * events = pd->events;
    struct buffer* shm = pd->shm;
    size_t len=0;
    char* tmpline=0;
    size_t tmpline_len=0;
    int status=0;
    const char* delim = pd->delim;
    size_t exprs_num = pd->exprs_num;
    struct event * ev = &pd->ev;
    signature_operand op;
    for(char* line=buf_get_upto(buf, delim);line!=NULL;line=(free(line),buf_get_upto(buf, delim)))
    {
        len=strlen(line);
        
        #ifdef WITH_LINES
                ++ev.line;
        #endif
        if(len==0)
        {
            continue;
        }
        line[len-1] = '\0';
        for (int i = 0; i < (int)exprs_num; ++i) {
            if (events[i].kind == 0)
                continue; /* monitor is not interested in this */

            status = regexec(&pd->re[i], line, MAXMATCH, matches, 0);
            if (status != 0) {
                continue;
            }
            printf("{");
            int m = 1;
            void *addr;
            while (!(addr = buffer_start_push(shm))) {
                ++waiting_for_buffer;
            }
            /* push the base info about event */
            ++ev->base.id;
            ev->base.kind = events[i].kind;
            addr = buffer_partial_push(shm, addr, ev, sizeof(struct event));

            /* push the arguments of the event */
            for (const char *o = signatures[i]; *o && m <= MAXMATCH; ++o, ++m) {
                if (m > 1)
                    printf(", ");

                if (*o == 'L') { /* user wants the whole line */
                    printf("'%s'", line);
                    addr = buffer_partial_push_str(shm, addr, ev->base.id, line);
                    continue;
                }
                if (*o != 'M') {
                    if ((int)matches[m].rm_so < 0) {
                        fprintf(stderr, "warning: have no match for '%c' in signature %s\n", *o, signatures[i]);
                        continue;
                    }
                    len = matches[m].rm_eo - matches[m].rm_so;
                } else {
                    len = matches[0].rm_eo - matches[0].rm_so;
                }

                /* make sure we have big enough temporary buffer */
                if (tmpline_len < (size_t)len) {
                    free(tmpline);
                    tmpline = malloc(sizeof(char)*len+1);
                    assert(tmpline && "Memory allocation failed");
                    tmpline_len = len;
                }

                if (*o == 'M') { /* user wants the whole match */
                    assert(matches[0].rm_so >= 0);
                    strncpy(tmpline, line+matches[0].rm_so, len);
                    tmpline[len] = '\0';
                    addr = buffer_partial_push_str(shm, addr, ev->base.id, tmpline);
                    printf("'%s'",  tmpline);
                    continue;
                } else {
                    strncpy(tmpline, line+matches[m].rm_so, len);
                    tmpline[len] = '\0';
                }

                switch(*o) {
                    case 'c':
                        assert(len == 1);
                        printf("%c", *(char*)(line + matches[m].rm_eo));
                        addr = buffer_partial_push(shm, addr,
                                                (char*)(line + matches[m].rm_eo),
                                                sizeof(op.c));
                        break;
                    case 'i': op.i = atoi(tmpline);
                            printf("%d", op.i);
                            addr = buffer_partial_push(shm, addr, &op.i, sizeof(op.i));
                            break;
                    case 'l': op.l = atol(tmpline);
                            printf("%ld", op.l);
                            addr = buffer_partial_push(shm, addr, &op.l, sizeof(op.l));
                            break;
                    case 'f': op.f = atof(tmpline);
                            printf("%lf", op.f);
                            addr = buffer_partial_push(shm, addr, &op.f, sizeof(op.f));
                            break;
                    case 'd': op.d = strtod(tmpline, NULL);
                            printf("%lf", op.d);
                            addr = buffer_partial_push(shm, addr, &op.d, sizeof(op.d));
                            break;
                    case 'S': printf("'%s'", tmpline);
                            addr = buffer_partial_push_str(shm, addr, ev->base.id, tmpline);
                            break;
                    default: assert(0 && "Invalid signature");
                }
            }
            buffer_finish_push(shm);
            printf("}\n");
            break;
        }
    }
}

static inline void push_event(bool iswrite, per_thread_t *data, ssize_t retlen) {
    if(iswrite)
    {
        insert_message(&outbuf, (char*)data->buf, retlen);
        process_messages(&outbuf, pd_out);
    }
    else
    {
        insert_message(&inbuf, (char*)data->buf, retlen);
        process_messages(&inbuf, pd_in);
    }
}

DR_EXPORT void dr_client_main(client_id_t id, int argc, const char *argv[]) {
    const char* indelim;
    const char* outdelim;
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
        dr_fprintf(STDERR, "Client DrRegexRW is running\n");
    }
    if (argc < 12)
	{
        usage_and_exit(1);
	}
    
    if (argc < 12 && (argc - 6) % 3 != 0) {
        usage_and_exit(1);
    }

	//pid_t process_id = atoi(argv[1]);
    outdelim=argv[3];
    size_t exprs_num_out = 0;
    size_t exprs_num_in = 0;

    int argpos = 4;
    while(argpos<argc&&strncmp(argv[argpos],"-/-",4)!=0)
    {
        argpos+=3;
        exprs_num_out++;
    }
    argpos++;
    indelim=argv[argpos];
    argpos++;
    while(argpos+3<=argc)
    {
        argpos+=3;
        exprs_num_in++;
    }

    pd_out=(parsedata*)alloca(sizeof(parsedata)+(sizeof(regex_t)*exprs_num_out));
    pd_in=(parsedata*)alloca(sizeof(parsedata)+(sizeof(regex_t)*exprs_num_in));
    const char *shmkey = argv[2];
    char *shmkey_name_out = alloca(sizeof(char)*(strlen(shmkey)+5));
    char *shmkey_name_in = alloca(sizeof(char)*(strlen(shmkey)+4));
    strncpy(shmkey_name_out, shmkey, strlen(shmkey));
    strncpy(shmkey_name_in, shmkey, strlen(shmkey));
    strncpy(shmkey_name_out+strlen(shmkey), "_out", 5);
    strncpy(shmkey_name_in+strlen(shmkey), "_in", 5);
    *(shmkey_name_out+strlen(shmkey)+4)=0;
    *(shmkey_name_in+strlen(shmkey)+3)=0;
    const char *exprs_out[exprs_num_out];
    const char *signatures_out[exprs_num_out];
    const char *names_out[exprs_num_out];
    const char *exprs_in[exprs_num_in];
    const char *signatures_in[exprs_num_in];
    const char *names_in[exprs_num_in];
    pd_out->exprs=exprs_out;
    pd_out->signatures=signatures_out;
    pd_out->names=names_out;
    pd_out->exprs_num=exprs_num_out;
    pd_in->exprs=exprs_in;
    pd_in->signatures=signatures_in;
    pd_in->names=names_in;
    pd_in->exprs_num=exprs_num_in;
    
    argpos=4;
    for (int i = 0; i < (int)exprs_num_out; i++) {
        names_out[i] = argv[argpos++];
        exprs_out[i] = argv[argpos++];
        signatures_out[i] = argv[argpos++];

        /* compile the regex, use extended RE */
        int status = regcomp(&pd_out->re[i], exprs_out[i], REG_EXTENDED);
        if (status != 0) {
            fprintf(stderr, "Failed compiling regex '%s'\n", exprs_out[i]);
            /* FIXME: we leak the expressions compiled so far ... */
            exit(1);
        }
    }
    argpos++;
    argpos++;
    for (int i = 0; i < (int)exprs_num_in; i++) {
        names_in[i] = argv[argpos++];
        exprs_in[i] = argv[argpos++];
        signatures_in[i] = argv[argpos++];

        /* compile the regex, use extended RE */
        int status = regcomp(&pd_in->re[i], exprs_in[i], REG_EXTENDED);
        if (status != 0) {
            fprintf(stderr, "Failed compiling regex '%s'\n", exprs_in[i]);
            /* FIXME: we leak the expressions compiled so far ... */
            exit(1);
        }
    }

    /* Initialize the info about the sources */
    struct source_control *control_in = source_control_define_pairwise(exprs_num_in,
                                                                       (const char **)names_in,
                                                                       (const char **)signatures_in);
    assert(control_in);
    struct source_control *control_out = source_control_define_pairwise(exprs_num_out,
                                                                       (const char **)names_out,
                                                                       (const char **)signatures_out);
    assert(control_out);

    struct buffer *shm_out = create_shared_buffer(shmkey_name_out, control_out);
    struct buffer *shm_in = create_shared_buffer(shmkey_name_in, control_in);
    assert(shm_out);
    assert(shm_in);
    free(control_out);
    free(control_in);
    pd_out->shm=shm_out;
    pd_out->delim=outdelim;
    pd_out->ev.base.id=3;
    pd_in->shm=shm_in;
    pd_in->delim=indelim;
    pd_in->ev.base.id=3;
    fprintf(stderr, "info: waiting for the monitor to attach\n");
    buffer_wait_for_monitor(shm_out);
    buffer_wait_for_monitor(shm_in);
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
               pd_in->ev.base.id + pd_out->ev.base.id, waiting_for_buffer);
    for (int i = 0; i < (int)pd_in->exprs_num; ++i) {
        regfree(&pd_in->re[i]);
    }
    for (int i = 0; i < (int)pd_out->exprs_num; ++i) {
        regfree(&pd_out->re[i]);
    }

    // free(tmpline);
    // free(partial_line);

    dr_printf("Destroying shared buffers\n");
    destroy_shared_buffer(pd_in->shm);
    destroy_shared_buffer(pd_out->shm);
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
    if(data->fd>2)
    {
        return;
    }
    /* right now we can handle just *one* filedescriptor (we have just one
     * buffer for incomplete lines), so use stdout */
    // if (data->fd != 1) {
    //     return;
    // }
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

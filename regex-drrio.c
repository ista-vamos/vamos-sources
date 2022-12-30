#include <alloca.h>
#include <assert.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include "event.h"
#include "fastbuf/shm_monitor.h"
#include "shmbuf/buffer.h"
#include "shmbuf/client.h"
#include "signatures.h"
#include "source.h"

#define MAXMATCH 20

static void usage_and_exit(int ret) {
    fprintf(stderr,
            "Usage: regex PID shmkey name expr sig [name expr sig] ...\n");
    exit(ret);
}

// #define WITH_LINES
// #define WITH_STDOUT

#ifndef WITH_STDOUT
#define printf(...) \
    do {            \
    } while (0)
#endif

struct event {
    shm_event base;
#ifdef WITH_LINES
    size_t line;
#endif
    unsigned char args[];
};

int monitoring_active = 1;
int do_print = 0;
size_t processed_bytes = 0;

typedef struct msgbuf {
    struct msgbuf *next;
    struct msgbuf *prev;
    char *textbuf;
    size_t offset;
} msgbuf;

void insert_message(msgbuf *buf, char *text) {
    if (buf->textbuf == NULL) {
        buf->textbuf = text;
        buf->offset = sizeof(size_t) + sizeof(int64_t);
    } else {
        msgbuf *newbuf = (msgbuf *)malloc(sizeof(msgbuf));
        newbuf->textbuf = text;
        newbuf->offset = sizeof(size_t) + sizeof(int64_t);
        newbuf->next = buf;
        newbuf->prev = buf->prev;
        newbuf->next->prev = newbuf;
        newbuf->prev->next = newbuf;
    }
}

char *buf_get_upto(msgbuf *buf, char *delim) {
    msgbuf *current = buf;
    size_t size = 0;
    if (current->textbuf == NULL) {
        return NULL;
    }
    do {
        char *nlpos = strstr(current->textbuf, delim);
        if (nlpos != NULL) {
            size_t len = (((intptr_t)nlpos) - ((intptr_t)current->textbuf)) +
                         strlen(delim);
            char *ret = (char *)malloc(size + len + 1);
            memcpy(ret + size, current->textbuf, len);
            ret[size + len] = 0;
            size_t curlen = strlen(current->textbuf);
            if (curlen == len) {
                free(current->textbuf - current->offset);
                current->textbuf = NULL;
                if (current == buf) {
                    if (current->next != current) {
                        current->textbuf = current->next->textbuf;
                        current->offset = current->next->offset;
                        msgbuf *newnext = current->next->next;
                        newnext->prev = current;
                        free(current->next);
                        current->next = newnext;
                    }
                    return ret;
                } else {
                    current->next->prev = current->prev;
                    current->prev->next = current->next;
                }
                msgbuf *prev = current->prev;
                free(current);
                current = prev;
            } else {
                current->textbuf += len;
                current->offset += len;
                if (current == buf) {
                    return ret;
                }
                current = current->prev;
            }
            while (1) {
                size_t curlen = strlen(current->textbuf);
                size -= curlen;
                memcpy(ret + size, current->textbuf, curlen);
                free(current->textbuf - current->offset);
                current->textbuf = NULL;
                if (current == buf) {
                    if (current->next != current) {
                        current->textbuf = current->next->textbuf;
                        current->offset = current->next->offset;
                        msgbuf *newnext = current->next->next;
                        newnext->prev = current;
                        free(current->next);
                        current->next = newnext;
                    }
                    return ret;
                } else {
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

char *buf_get_line(msgbuf *buf) { return buf_get_upto(buf, "\n"); }

static size_t waiting_for_buffer = 0;

typedef struct _parsedata {
    size_t exprs_num;
    char **exprs;
    char **signatures;
    char **names;
    struct buffer *shm;
    struct event_record *events;
    regex_t re[];
} parsedata;
parsedata *pd;

int monitoring_thread(void *arg) {
    size_t exprs_num = pd->exprs_num;
    struct event_record *events = pd->events;
    char **signatures = pd->signatures;
    struct buffer *shm = pd->shm;
    regmatch_t matches[MAXMATCH + 1];

    int status;
    ssize_t len;
    // size_t line_len;
    char *tmpline = NULL;
    size_t tmpline_len = 0;
    signature_operand op;

    struct event ev;
    memset(&ev, 0, sizeof(ev));

    monitor_buffer buffer = (monitor_buffer)arg;
    buffer_entry buffer_buffer[32];
    // msgbuf read_msg;
    msgbuf write_msg;
    // read_msg.next = &read_msg;
    write_msg.next = &write_msg;
    // read_msg.prev = &read_msg;
    write_msg.prev = &write_msg;
    // read_msg.textbuf = NULL;
    write_msg.textbuf = NULL;
    while (monitoring_active) {
#ifdef SHM_DOMONITOR_NOWAIT
        size_t count = copy_events_nowait(buffer, buffer_buffer, 32);
#else
        size_t count = copy_events_wait(buffer, buffer_buffer, 32);
#endif
        for (size_t i = 0; i < count; i++) {
            if (buffer_buffer[i].kind == 1) {
                insert_message(&write_msg,
                               ((char *)(buffer_buffer[i].payload64_1)) +
                                   sizeof(size_t) + sizeof(int64_t));
            } else {
                if (buffer_buffer[i].payload64_1 != 0) {
                    free((char *)(buffer_buffer[i].payload64_1));
                }
            }
            // else
            // {
            // 	insert_message(&read_msg, ((char
            // *)(buffer_buffer[i].payload64_1)) + sizeof(size_t) +
            // sizeof(int64_t));
            // }
            // process_messages(&write_msg);
            processed_bytes += buffer_buffer[i].payload64_2;
        }

        char *line = buf_get_upto(&write_msg, "\n");
        for (; line != NULL;
             line = (free(line), buf_get_upto(&write_msg, "\n"))) {
            len = strlen(line);

#ifdef WITH_LINES
            ++ev.line;
#endif
            if (len == 0) {
                continue;
            }
            line[len - 1] = '\0';
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
                ++ev.base.id;
                ev.base.kind = events[i].kind;
                addr = buffer_partial_push(shm, addr, &ev, sizeof(ev));

                /* push the arguments of the event */
                for (const char *o = signatures[i]; *o && m <= MAXMATCH;
                     ++o, ++m) {
                    if (m > 1)
                        printf(", ");

                    if (*o == 'L') { /* user wants the whole line */
                        printf("'%s'", line);
                        addr = buffer_partial_push_str(shm, addr, ev.base.id,
                                                       line);
                        continue;
                    }
                    if (*o != 'M') {
                        if ((int)matches[m].rm_so < 0) {
                            fprintf(stderr,
                                    "warning: have no match for '%c' in "
                                    "signature %s\n",
                                    *o, signatures[i]);
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
                        addr = buffer_partial_push_str(shm, addr, ev.base.id,
                                                       tmpline);
                        printf("'%s'", tmpline);
                        continue;
                    } else {
                        strncpy(tmpline, line + matches[m].rm_so, len);
                        tmpline[len] = '\0';
                    }

                    switch (*o) {
                        case 'c':
                            assert(len == 1);
                            printf("%c", *(char *)(line + matches[m].rm_eo));
                            addr = buffer_partial_push(
                                shm, addr, (char *)(line + matches[m].rm_eo),
                                sizeof(op.c));
                            break;
                        case 'i':
                            op.i = atoi(tmpline);
                            printf("%d", op.i);
                            addr = buffer_partial_push(shm, addr, &op.i,
                                                       sizeof(op.i));
                            break;
                        case 'l':
                            op.l = atol(tmpline);
                            printf("%ld", op.l);
                            addr = buffer_partial_push(shm, addr, &op.l,
                                                       sizeof(op.l));
                            break;
                        case 'f':
                            op.f = atof(tmpline);
                            printf("%lf", op.f);
                            addr = buffer_partial_push(shm, addr, &op.f,
                                                       sizeof(op.f));
                            break;
                        case 'd':
                            op.d = strtod(tmpline, NULL);
                            printf("%lf", op.d);
                            addr = buffer_partial_push(shm, addr, &op.d,
                                                       sizeof(op.d));
                            break;
                        case 'S':
                            printf("'%s'", tmpline);
                            addr = buffer_partial_push_str(shm, addr,
                                                           ev.base.id, tmpline);
                            break;
                        default:
                            assert(0 && "Invalid signature");
                    }
                }
                buffer_finish_push(shm);
                printf("}\n");
            }
        }
    }
    fprintf(stderr, "info: sent %lu events, busy waited on buffer %lu cycles\n",
            ev.base.id, waiting_for_buffer);
    free(tmpline);
    // free(line);
    return 0;
}

int register_monitored_thread(monitor_buffer buffer) {
    thrd_t thrd;
    thrd_create(&thrd, &monitoring_thread, buffer);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 6) {
        printf("Usage: monitor PID shmkey name expr sig [name expr sig] ...\n");
        return 1;
    }

    if (argc < 6 && (argc - 2) % 3 != 0) {
        usage_and_exit(1);
    }

    pid_t process_id = atoi(argv[1]);
    argc--;
    argv++;

    size_t exprs_num = (argc - 1) / 3;
    if (exprs_num == 0) {
        usage_and_exit(1);
    }

    pd = (parsedata *)alloca(sizeof(parsedata) + (sizeof(regex_t) * exprs_num));
    const char *shmkey = argv[1];
    char *exprs[exprs_num];
    char *signatures[exprs_num];
    char *names[exprs_num];
    pd->exprs = exprs;
    pd->signatures = signatures;
    pd->names = names;
    pd->exprs_num = exprs_num;

    int arg_i = 2;
    for (int i = 0; i < (int)exprs_num; ++i) {
        names[i] = argv[arg_i++];
        exprs[i] = argv[arg_i++];
        if (arg_i >= argc) {
            fprintf(stderr, "Missing a signature for '%s'\n", exprs[i]);
            usage_and_exit(1);
        }
        signatures[i] = argv[arg_i++];

        /* compile the regex, use extended RE */
        int status = regcomp(&pd->re[i], exprs[i], REG_EXTENDED);
        if (status != 0) {
            fprintf(stderr, "Failed compiling regex '%s'\n", exprs[i]);
            /* FIXME: we leak the expressions compiled so far ... */
            exit(1);
        }
    }

    /* Initialize the info about this source */
    struct source_control *control = source_control_define_pairwise(
        exprs_num, (const char **)names, (const char **)signatures);
    assert(control);
    const size_t capacity = 256;
    struct buffer *shm = create_shared_buffer(shmkey, capacity, control);
    assert(shm);
    free(control);

    pd->shm = shm;
    fprintf(stderr, "info: waiting for the monitor to attach\n");
    buffer_wait_for_monitor(shm);

    size_t num;
    pd->events = buffer_get_avail_events(shm, &num);
    assert(num == exprs_num);

    monitored_process proc =
        attach_to_process(process_id, &register_monitored_thread);

    wait_for_process(proc);
    monitoring_active = 0;

    printf("Processed bytes: %lu\n", processed_bytes);
    /* Free up memory held within the regex memory */
    for (int i = 0; i < (int)exprs_num; ++i) {
        regfree(&pd->re[i]);
    }

    destroy_shared_buffer(shm);
    return 0;
}

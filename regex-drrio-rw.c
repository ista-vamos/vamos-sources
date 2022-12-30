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
    printf(
        "Usage: monitor PID shmkey outsplit name expr sig [name expr sig] "
        "... -- insplit name expr sig [name expr sig] ...\n");
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
char *indelim = NULL;
char *outdelim = NULL;

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
parsedata *pd_out;
parsedata *pd_in;

int monitoring_thread(void *arg) {
    size_t exprs_num_in = pd_in->exprs_num;
    size_t exprs_num_out = pd_out->exprs_num;
    struct event_record *events_out = pd_out->events;
    struct event_record *events_in = pd_in->events;
    char **signatures_in = pd_in->signatures;
    struct buffer *shm_in = pd_in->shm;
    char **signatures_out = pd_out->signatures;
    struct buffer *shm_out = pd_out->shm;
    regmatch_t matches[MAXMATCH + 1];

    int status;
    ssize_t len;
    // size_t line_len;
    char *tmpline = NULL;
    size_t tmpline_len = 0;
    signature_operand op;

    struct event ev_out;
    memset(&ev_out, 0, sizeof(ev_out));
    struct event ev_in;
    memset(&ev_in, 0, sizeof(ev_in));

    monitor_buffer buffer = (monitor_buffer)arg;
    buffer_entry buffer_buffer[32];
    msgbuf read_msg;
    msgbuf write_msg;
    read_msg.next = &read_msg;
    write_msg.next = &write_msg;
    read_msg.prev = &read_msg;
    write_msg.prev = &write_msg;
    read_msg.textbuf = NULL;
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
                insert_message(&read_msg,
                               ((char *)(buffer_buffer[i].payload64_1)) +
                                   sizeof(size_t) + sizeof(int64_t));
            }
            processed_bytes += buffer_buffer[i].payload64_2;
        }

        char *line = buf_get_upto(&write_msg, outdelim);
        for (; line != NULL;
             line = (free(line), buf_get_upto(&write_msg, outdelim))) {
            len = strlen(line);

#ifdef WITH_LINES
            ++ev.line;
#endif
            if (len == 0) {
                continue;
            }
            line[len - 1] = '\0';
            for (int i = 0; i < (int)exprs_num_out; ++i) {
                if (events_out[i].kind == 0)
                    continue; /* monitor is not interested in this */

                status = regexec(&pd_out->re[i], line, MAXMATCH, matches, 0);
                if (status != 0) {
                    continue;
                }
                printf("{");
                int m = 1;
                void *addr;
                while (!(addr = buffer_start_push(shm_out))) {
                    ++waiting_for_buffer;
                }
                /* push the base info about event */
                ++ev_out.base.id;
                ev_out.base.kind = events_out[i].kind;
                addr =
                    buffer_partial_push(shm_out, addr, &ev_out, sizeof(ev_out));

                /* push the arguments of the event */
                for (const char *o = signatures_out[i]; *o && m <= MAXMATCH;
                     ++o, ++m) {
                    if (m > 1)
                        printf(", ");

                    if (*o == 'L') { /* user wants the whole line */
                        printf("'%s'", line);
                        addr = buffer_partial_push_str(shm_out, addr,
                                                       ev_out.base.id, line);
                        continue;
                    }
                    if (*o != 'M') {
                        if ((int)matches[m].rm_so < 0) {
                            fprintf(stderr,
                                    "warning: have no match for '%c' in "
                                    "signature %s\n",
                                    *o, signatures_out[i]);
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
                        addr = buffer_partial_push_str(shm_out, addr,
                                                       ev_out.base.id, tmpline);
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
                                shm_out, addr,
                                (char *)(line + matches[m].rm_eo),
                                sizeof(op.c));
                            break;
                        case 'i':
                            op.i = atoi(tmpline);
                            printf("%d", op.i);
                            addr = buffer_partial_push(shm_out, addr, &op.i,
                                                       sizeof(op.i));
                            break;
                        case 'l':
                            op.l = atol(tmpline);
                            printf("%ld", op.l);
                            addr = buffer_partial_push(shm_out, addr, &op.l,
                                                       sizeof(op.l));
                            break;
                        case 'f':
                            op.f = atof(tmpline);
                            printf("%lf", op.f);
                            addr = buffer_partial_push(shm_out, addr, &op.f,
                                                       sizeof(op.f));
                            break;
                        case 'd':
                            op.d = strtod(tmpline, NULL);
                            printf("%lf", op.d);
                            addr = buffer_partial_push(shm_out, addr, &op.d,
                                                       sizeof(op.d));
                            break;
                        case 'S':
                            printf("'%s'", tmpline);
                            addr = buffer_partial_push_str(
                                shm_out, addr, ev_out.base.id, tmpline);
                            break;
                        default:
                            assert(0 && "Invalid signature");
                    }
                }
                buffer_finish_push(shm_out);
                printf("}\n");
                break;
            }
        }

        line = buf_get_upto(&read_msg, indelim);
        for (; line != NULL;
             line = (free(line), buf_get_upto(&read_msg, indelim))) {
            len = strlen(line);

#ifdef WITH_LINES
            ++ev.line;
#endif
            if (len == 0) {
                continue;
            }
            line[len - 1] = '\0';
            for (int i = 0; i < (int)exprs_num_in; ++i) {
                if (events_in[i].kind == 0)
                    continue; /* monitor is not interested in this */

                status = regexec(&pd_in->re[i], line, MAXMATCH, matches, 0);
                if (status != 0) {
                    continue;
                }
                printf("{");
                int m = 1;
                void *addr;
                while (!(addr = buffer_start_push(shm_in))) {
                    ++waiting_for_buffer;
                }
                /* push the base info about event */
                ++ev_in.base.id;
                ev_in.base.kind = events_in[i].kind;
                addr = buffer_partial_push(shm_in, addr, &ev_in, sizeof(ev_in));

                /* push the arguments of the event */
                for (const char *o = signatures_in[i]; *o && m <= MAXMATCH;
                     ++o, ++m) {
                    if (m > 1)
                        printf(", ");

                    if (*o == 'L') { /* user wants the whole line */
                        printf("'%s'", line);
                        addr = buffer_partial_push_str(shm_in, addr,
                                                       ev_in.base.id, line);
                        continue;
                    }
                    if (*o != 'M') {
                        if ((int)matches[m].rm_so < 0) {
                            fprintf(stderr,
                                    "warning: have no match for '%c' in "
                                    "signature %s\n",
                                    *o, signatures_in[i]);
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
                        addr = buffer_partial_push_str(shm_in, addr,
                                                       ev_in.base.id, tmpline);
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
                                shm_in, addr, (char *)(line + matches[m].rm_eo),
                                sizeof(op.c));
                            break;
                        case 'i':
                            op.i = atoi(tmpline);
                            printf("%d", op.i);
                            addr = buffer_partial_push(shm_in, addr, &op.i,
                                                       sizeof(op.i));
                            break;
                        case 'l':
                            op.l = atol(tmpline);
                            printf("%ld", op.l);
                            addr = buffer_partial_push(shm_in, addr, &op.l,
                                                       sizeof(op.l));
                            break;
                        case 'f':
                            op.f = atof(tmpline);
                            printf("%lf", op.f);
                            addr = buffer_partial_push(shm_in, addr, &op.f,
                                                       sizeof(op.f));
                            break;
                        case 'd':
                            op.d = strtod(tmpline, NULL);
                            printf("%lf", op.d);
                            addr = buffer_partial_push(shm_in, addr, &op.d,
                                                       sizeof(op.d));
                            break;
                        case 'S':
                            printf("'%s'", tmpline);
                            addr = buffer_partial_push_str(
                                shm_in, addr, ev_in.base.id, tmpline);
                            break;
                        default:
                            assert(0 && "Invalid signature");
                    }
                }
                buffer_finish_push(shm_in);
                printf("}\n");
                break;
            }
        }
    }
    fprintf(stderr, "info: sent %lu events, busy waited on buffer %lu cycles\n",
            (ev_in.base.id + ev_out.base.id), waiting_for_buffer);
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
    if (argc < 12) {
        usage_and_exit(1);
    }

    if (argc < 12 && (argc - 6) % 3 != 0) {
        usage_and_exit(1);
    }

    pid_t process_id = atoi(argv[1]);
    outdelim = argv[3];
    size_t exprs_num_out = 0;
    size_t exprs_num_in = 0;

    int argpos = 4;
    while (argpos < argc && strncmp(argv[argpos], "--", 3) != 0) {
        argpos += 3;
        exprs_num_out++;
    }
    argpos++;
    indelim = argv[argpos];
    argpos++;
    while (argpos + 3 <= argc) {
        argpos += 3;
        exprs_num_in++;
    }

    pd_out = (parsedata *)alloca(sizeof(parsedata) +
                                 (sizeof(regex_t) * exprs_num_out));
    pd_in = (parsedata *)alloca(sizeof(parsedata) +
                                (sizeof(regex_t) * exprs_num_in));
    const char *shmkey = argv[2];
    char *shmkey_name_out = alloca(sizeof(char) * (strlen(shmkey) + 5));
    char *shmkey_name_in = alloca(sizeof(char) * (strlen(shmkey) + 4));
    strncpy(shmkey_name_out, shmkey, strlen(shmkey));
    strncpy(shmkey_name_in, shmkey, strlen(shmkey));
    strncpy(shmkey_name_out + strlen(shmkey), "_out", 5);
    strncpy(shmkey_name_in + strlen(shmkey), "_in", 5);
    *(shmkey_name_out + strlen(shmkey) + 4) = 0;
    *(shmkey_name_in + strlen(shmkey) + 3) = 0;
    char *exprs_out[exprs_num_out];
    char *signatures_out[exprs_num_out];
    char *names_out[exprs_num_out];
    char *exprs_in[exprs_num_in];
    char *signatures_in[exprs_num_in];
    char *names_in[exprs_num_in];
    pd_out->exprs = exprs_out;
    pd_out->signatures = signatures_out;
    pd_out->names = names_out;
    pd_out->exprs_num = exprs_num_out;
    pd_in->exprs = exprs_in;
    pd_in->signatures = signatures_in;
    pd_in->names = names_in;
    pd_in->exprs_num = exprs_num_in;

    argpos = 4;
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
    struct source_control *control_in = source_control_define_pairwise(
        exprs_num_in, (const char **)names_in, (const char **)signatures_in);
    assert(control_in);
    struct source_control *control_out = source_control_define_pairwise(
        exprs_num_out, (const char **)names_out, (const char **)signatures_out);
    assert(control_out);

    const size_t capacity = 256;
    struct buffer *shm_out =
        create_shared_buffer(shmkey_name_out, capacity, control_out);
    struct buffer *shm_in =
        create_shared_buffer(shmkey_name_in, capacity, control_in);
    assert(shm_out);
    assert(shm_in);
    free(control_out);
    free(control_in);
    pd_out->shm = shm_out;
    pd_in->shm = shm_in;
    fprintf(stderr, "info: waiting for the monitor to attach\n");
    buffer_wait_for_monitor(shm_out);
    buffer_wait_for_monitor(shm_in);

    size_t num;
    pd_out->events = buffer_get_avail_events(shm_out, &num);
    assert(num == exprs_num_out);
    pd_in->events = buffer_get_avail_events(shm_in, &num);
    assert(num == exprs_num_in);

    monitored_process proc =
        attach_to_process(process_id, &register_monitored_thread);

    wait_for_process(proc);
    monitoring_active = 0;

    // printf("Processed bytes: %lu\n", processed_bytes);
    /* Free up memory held within the regex memory */
    for (int i = 0; i < (int)exprs_num_out; ++i) {
        regfree(&pd_out->re[i]);
    }
    for (int i = 0; i < (int)exprs_num_in; ++i) {
        regfree(&pd_in->re[i]);
    }

    destroy_shared_buffer(shm_in);
    destroy_shared_buffer(shm_out);
    return 0;
}

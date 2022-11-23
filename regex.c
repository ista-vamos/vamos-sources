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
    fprintf(stderr, "Usage: regex shmkey name expr sig [name expr sig] ...\n");
    exit(ret);
}

// #define WITH_LINES
// #define WITH_STDOUT

#ifndef WITH_STDOUT
#define printf(...)                                                            \
    do {                                                                       \
    } while (0)
#endif

struct event {
    shm_event base;
#ifdef WITH_LINES
    size_t line;
#endif
    unsigned char args[];
};

static size_t waiting_for_buffer = 0;

int main(int argc, char *argv[]) {
    if (argc < 5 && (argc - 2) % 3 != 0) {
        usage_and_exit(1);
    }

    size_t exprs_num = (argc - 1) / 3;
    if (exprs_num == 0) {
        usage_and_exit(1);
    }

    const char *shmkey = argv[1];
    char *exprs[exprs_num];
    char *signatures[exprs_num];
    char *names[exprs_num];
    regex_t re[exprs_num];

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
        int status = regcomp(&re[i], exprs[i], REG_EXTENDED);
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
    struct buffer *shm = create_shared_buffer(shmkey, control);
    assert(shm);
    free(control);

    fprintf(stderr, "info: waiting for the monitor to attach... ");
    buffer_wait_for_monitor(shm);
    fprintf(stderr, "done\n");

    regmatch_t matches[MAXMATCH + 1];

    int status;
    ssize_t len;
    size_t line_len;
    char *line = NULL;
    char *tmpline = NULL;
    size_t tmpline_len = 0;
    signature_operand op;

    struct event ev;
    memset(&ev, 0, sizeof(ev));
    size_t num;
    struct event_record *events = buffer_get_avail_events(shm, &num);
    assert(num == exprs_num && "Information in shared memory does not fit");

    while (1) {
        len = getline(&line, &line_len, stdin);
        if (len == -1)
            break;
        if (len == 0)
            continue;

#ifdef WITH_LINES
        ++ev.line;
#endif

        /* remove newline from the line */
        line[len - 1] = '\0';

        for (int i = 0; i < (int)exprs_num; ++i) {
            if (events[i].kind == 0)
                continue; /* monitor is not interested in this */

            status = regexec(&re[i], line, MAXMATCH, matches, 0);
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
            for (const char *o = signatures[i]; *o && m <= MAXMATCH; ++o, ++m) {
                if (m > 1)
                    printf(", ");

                if (*o == 'L') { /* user wants the whole line */
                    printf("'%s'", line);
                    addr = buffer_partial_push_str(shm, addr, ev.base.id, line);
                    continue;
                }
                if (*o != 'M') {
                    if ((int)matches[m].rm_so < 0) {
                        fprintf(
                            stderr,
                            "warning: have no match for '%c' in signature %s\n",
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
                    addr =
                        buffer_partial_push_str(shm, addr, ev.base.id, tmpline);
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
                    addr = buffer_partial_push(shm, addr, &op.i, sizeof(op.i));
                    break;
                case 'l':
                    op.l = atol(tmpline);
                    printf("%ld", op.l);
                    addr = buffer_partial_push(shm, addr, &op.l, sizeof(op.l));
                    break;
                case 'f':
                    op.f = atof(tmpline);
                    printf("%lf", op.f);
                    addr = buffer_partial_push(shm, addr, &op.f, sizeof(op.f));
                    break;
                case 'd':
                    op.d = strtod(tmpline, NULL);
                    printf("%lf", op.d);
                    addr = buffer_partial_push(shm, addr, &op.d, sizeof(op.d));
                    break;
                case 'S':
                    printf("'%s'", tmpline);
                    addr =
                        buffer_partial_push_str(shm, addr, ev.base.id, tmpline);
                    break;
                default:
                    assert(0 && "Invalid signature");
                }
            }
            buffer_finish_push(shm);
            printf("}\n");
        }
    }

    /* Free up memory held within the regex memory */
    fprintf(stderr, "info: sent %lu events, busy waited on buffer %lu cycles\n",
            ev.base.id, waiting_for_buffer);
    free(tmpline);
    free(line);
    for (int i = 0; i < (int)exprs_num; ++i) {
        regfree(&re[i]);
    }

    destroy_shared_buffer(shm);

    return 0;
}

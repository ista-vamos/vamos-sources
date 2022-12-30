/* **********************************************************
 * Copyright (c) 2021-2022 IST Austria.  All rights reserved.
 * **********************************************************/

/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * * Neither the name of VMware, Inc. nor the names of its contributors may be
 *   used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL VMWARE, INC. OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

/*
 * We took inspiration in instrace_simple.c and instrcalls.c sample
 * tools from DynamoRIO.
 */

#define SHOW_SYMBOLS 1
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <unistd.h>

#include "dr_api.h"
#include "dr_defines.h"
#include "drmgr.h"
#include "drreg.h"
#include "drutil.h"
#ifdef SHOW_SYMBOLS
#include "drsyms.h"
#endif

#include "buffer.h"
#include "client.h"

#ifdef WINDOWS
#define IF_WINDOWS(x) x
#else
#define IF_WINDOWS(x) /* nothing */
#endif

#include "event.h" /* shm_event_dropped */
#include "signatures.h"
#include "source.h"
#include "stream-funs.h"

static void event_exit(void);

/* for some reason we need this...*/
#undef bool
#define bool char

static dr_emit_flags_t event_app_instruction(void *drcontext, void *tag,
                                             instrlist_t *bb, instr_t *instr,
                                             bool for_trace, bool translating,
                                             void *user_data);

static void event_thread_context_init(void *drcontext, bool new_depth);
static void event_thread_context_exit(void *drcontext, bool process_exit);

static struct buffer *top_shmbuffer;
static struct source_control *top_control;
static struct event_record *events;
unsigned long *addresses;
static size_t events_num;

typedef struct {
    size_t thread;
    struct buffer *shm;
    size_t waiting_for_buffer;
} per_thread_t;

/* Thread-context-local storage index from drmgr */
static int tcls_idx;
/* we'll number threads from 0 up */
static size_t thread_num = 0;

static void find_functions(void *drcontext, const module_data_t *mod,
                           char loaded) {
    (void)drcontext;
    (void)loaded;
    /*
    size_t modoffs;
    drsym_error_t sym_res = drsym_lookup_symbol(
        mod->full_path, trace_function.get_value().c_str(), &modoffs,
    DRSYM_DEMANGLE); if (sym_res == DRSYM_SUCCESS) { app_pc towrap = mod->start
    + modoffs; bool ok = drwrap_wrap(towrap, wrap_pre, NULL); DR_ASSERT(ok);
        dr_fprintf(STDERR, "wrapping %s!%s\n", mod->full_path,
                   trace_function.get_value().c_str());
                   */

    /*
    drsym_enumerate_symbols(mod->full_path, enumsym, 0, 0);

    dr_symbol_export_iterator_t* it =
    dr_symbol_export_iterator_start(mod->handle); dr_symbol_export_t *sym; while
    (dr_symbol_export_iterator_hasnext(it)) { sym =
    dr_symbol_export_iterator_next(it); dr_printf("exported symbol: %s\n",
    sym->name);
    }
    dr_symbol_export_iterator_stop(it);
    */

    size_t off;
    for (size_t i = 0; i < events_num; ++i) {
        drsym_error_t ok =
            drsym_lookup_symbol(mod->full_path, events[i].name, &off,
                                /* flags = */ DRSYM_DEMANGLE);
        if (ok == DRSYM_ERROR_LINE_NOT_AVAILABLE || ok == DRSYM_SUCCESS) {
            addresses[i] = (size_t)mod->start + off;
            events[i].size =
                signature_get_size((unsigned char *)events[i].signature) +
                sizeof(shm_event_funcall);
            dr_printf("Found %s:%s in %s at 0x%x (size %lu)\n", events[i].name,
                      events[i].signature, mod->full_path, addresses[i],
                      events[i].size);
        }
    }
}

DR_EXPORT void dr_client_main(client_id_t id, int argc, const char *argv[]) {
    (void)id;
    if (argc < 3) {
        dr_fprintf(STDERR,
                   "Need arguments shmkey 'fun1:[sig]' 'fun2:[sig]' ...\n");
        DR_ASSERT(0);
    }

    const char *shmkey = argv[1];
    events_num = argc - 2;
    dr_fprintf(STDERR, "shmkey: %s\n", shmkey);
    dr_fprintf(STDERR, "number of events: %lu\n", events_num);
    addresses = dr_global_alloc(events_num * sizeof(unsigned long));
    DR_ASSERT(addresses);

    const char *names[events_num];
    const char *signatures[events_num];

    for (size_t i = 0; i < events_num; ++i) {
        names[i] = argv[i + 2];
        char *colon = strchr(names[i], ':');
        if (colon) {
            *colon = 0;
            signatures[i] = colon + 1;
        } else {
            signatures[i] = "";
        }
        dr_fprintf(STDERR, "Registering event '%s' with signature '%s'\n",
                   names[i], signatures[i]);
    }

    /* Initialize the info about this source */
    top_control = source_control_define_pairwise(
        events_num, (const char **)names, (const char **)signatures);
    assert(top_control);

    dr_set_client_name("Track function calls", "");
    drmgr_init();
    /* make it easy to tell, by looking at log file, which client executed */
    dr_log(NULL, DR_LOG_ALL, 1, "Client 'drfun' initializing\n");
    tcls_idx = drmgr_register_cls_field(event_thread_context_init,
                                        event_thread_context_exit);
    DR_ASSERT(tcls_idx != -1);

    /* also give notification to stderr */
    if (dr_is_notify_on()) {
#ifdef WINDOWS
        /* ask for best-effort printing to cmd window.  must be called at init.
         */
        dr_enable_console_printing();
#endif
        dr_fprintf(STDERR, "Client instrcalls is running\n");
    }

    if (drsym_init(0) != DRSYM_SUCCESS) {
        dr_log(NULL, DR_LOG_ALL, 1,
               "WARNING: unable to initialize symbol translation\n");
    }
    dr_register_exit_event(event_exit);
    drmgr_register_module_load_event(find_functions);

    drmgr_register_bb_instrumentation_event(NULL, (void *)event_app_instruction,
                                            0);

    const size_t capacity = 256;
    top_shmbuffer = create_shared_buffer(shmkey, capacity, top_control);
    DR_ASSERT(top_shmbuffer);

    events = buffer_get_avail_events(top_shmbuffer, &events_num);

    dr_printf("Waiting for the monitor to attach\n");
    if (buffer_wait_for_monitor(top_shmbuffer) < 0) {
        perror("Waiting for the buffer failed");
        abort();
    }
}
static void event_thread_context_init(void *drcontext, bool new_depth) {
    /* create an instance of our data structure for this thread context */
    per_thread_t *data;
    if (new_depth) {
        data = (per_thread_t *)dr_thread_alloc(drcontext, sizeof(per_thread_t));
        drmgr_set_cls_field(drcontext, tcls_idx, data);
        data->thread = ++thread_num;
        /* TODO: for now we create a buffer of the same type as the top buffer
         */
        data->shm = create_shared_sub_buffer(top_shmbuffer, 0, top_control);
        data->waiting_for_buffer = 0;
        DR_ASSERT(data->shm && "Failed creating buffer");
    } else {
        data = (per_thread_t *)drmgr_get_cls_field(drcontext, tcls_idx);
    }
}

static void event_thread_context_exit(void *drcontext, bool thread_exit) {
    if (!thread_exit)
        return;
    per_thread_t *data =
        (per_thread_t *)drmgr_get_cls_field(drcontext, tcls_idx);
    dr_printf(
        "Thread %lu exits, looped in a busy wait for the buffer %lu times\n",
        data->thread, data->waiting_for_buffer);
    dr_printf("... (releasing shared buffer)\n");
    release_shared_buffer(data->shm);
    dr_thread_free(drcontext, data, sizeof(per_thread_t));
}

static void event_exit(void) {
#ifdef SHOW_SYMBOLS
    if (drsym_exit() != DRSYM_SUCCESS) {
        dr_log(NULL, DR_LOG_ALL, 1,
               "WARNING: error cleaning up symbol library\n");
    }
#endif
    drmgr_exit();
    destroy_shared_buffer(top_shmbuffer);
    dr_global_free(addresses, events_num * sizeof(unsigned long));
}

/* adapted from instrcalls.c */
static app_pc call_get_target(instr_t *instr) {
    app_pc target = 0;
    opnd_t targetop = instr_get_target(instr);
    if (opnd_is_pc(targetop)) {
        if (opnd_is_far_pc(targetop)) {
            DR_ASSERT(false && "call_get_target: far pc not supported");
        }
        target = (app_pc)opnd_get_pc(targetop);
    } else if (opnd_is_instr(targetop)) {
        DR_ASSERT(target != 0 && "call_get_target: unknown target");
    } else {
        DR_ASSERT(false && "call_get_target: unknown target");
        target = 0;
    }
    return target;
}

static inline void *call_get_arg_ptr(dr_mcontext_t *mc, int i, char o) {
    if (o == 'f') {
        DR_ASSERT(i < 7);
        return &mc->simd[i].u64;
    }
    DR_ASSERT(i < 6);
    switch (i) {
        case 0:
            return &mc->xdi;
        case 1:
            return &mc->xsi;
        case 2:
            return &mc->xdx;
        case 3:
            return &mc->xcx;
        case 4:
            return &mc->r8;
        case 5:
            return &mc->r9;
    }
    DR_ASSERT(0 && "Not implemented");
    return NULL;
}

static size_t last_event_id = 0;

static void at_call_generic(size_t fun_idx, const char *sig) {
    dr_mcontext_t mc = {sizeof(mc), DR_MC_INTEGER};
    void *drcontext = dr_get_current_drcontext();
    dr_get_mcontext(drcontext, &mc);

    per_thread_t *data =
        (per_thread_t *)drmgr_get_cls_field(drcontext, tcls_idx);
    struct buffer *shm = data->shm;
    void *shmaddr;
    while (!(shmaddr = buffer_start_push(shm))) {
        ++data->waiting_for_buffer;
    }
    DR_ASSERT(fun_idx < events_num);
    shm_event_funcall *ev = (shm_event_funcall *)shmaddr;
    ev->base.kind = events[fun_idx].kind;
    ev->base.id = ++last_event_id;
    memcpy(ev->signature, events[fun_idx].signature, sizeof(ev->signature));
    shmaddr = ev->args;
    DR_ASSERT(shmaddr && "Failed partial push");
    printf("Fun %lu -- %s\n", fun_idx, sig);
    int i = 0;
    for (const char *o = sig; *o; ++o) {
        switch (*o) {
            case '_':
                break;
            case 'S':
                shmaddr = buffer_partial_push_str(
                    shm, shmaddr, last_event_id,
                    *(const char **)call_get_arg_ptr(&mc, i, *o));
                break;
            default:
                shmaddr = buffer_partial_push(shm, shmaddr,
                                              call_get_arg_ptr(&mc, i, *o),
                                              signature_op_get_size(*o));
                /* printf(" arg %d=%ld", i, *(size_t*)call_get_arg_ptr(&mc, i,
                 * *o));
                 */
                break;
        }
        ++i;
    }
    buffer_finish_push(shm);
    /* putchar('\n'); */
}

static dr_emit_flags_t event_app_instruction(void *drcontext, void *tag,
                                             instrlist_t *bb, instr_t *instr,
                                             bool for_trace, bool translating,
                                             void *user_data) {
    (void)tag;
    (void)for_trace;
    (void)user_data;

    if (instr_is_meta(instr) || translating)
        return DR_EMIT_DEFAULT;

    if (instr_is_call_direct(instr)) {
        app_pc target = call_get_target(instr);
        for (size_t i = 0; i < events_num; ++i) {
            // dr_printf("   target 0x%x == 0x%x events[%lu].addr\n", target,
            // events[i].addr, i);
            if (target == (app_pc)addresses[i]) {
                if (events[i].kind == 0) {
                    dr_printf("Found a call of %s, but skipping\n",
                              events[i].name);
                    continue;  // monitor has no interest in this event
                }
                dr_printf("Found a call of %s\n", events[i].name);
                dr_insert_clean_call_ex(
                    drcontext, bb, instr, (app_pc)at_call_generic,
                    DR_CLEANCALL_READS_APP_CONTEXT, 2,
                    /* call target is 1st parameter */
                    OPND_CREATE_INT64(i),
                    /* signature is 2nd parameter */
                    OPND_CREATE_INTPTR(events[i].signature));
            }
        }
    }

    /* else if (instr_is_call_indirect(instr)) {
        dr_insert_mbr_instrumentation(drcontext, bb, instr, (app_pc)at_call_ind,
                                      SPILL_SLOT_1);

    } else if (instr_is_return(instr)) {
        dr_insert_mbr_instrumentation(drcontext, bb, instr, (app_pc)at_return,
                                      SPILL_SLOT_1);
    }
    */
    return DR_EMIT_DEFAULT;
}

/*
bool enumsym(const char *name, size_t off, void *data) {
    dr_printf("symbol: %s\n", name);
    return true;
}
*/

/* from instrcalls.c in DynamoRIO
static void
print_address(file_t f, app_pc addr, const char *prefix)
{
    drsym_error_t symres;
    drsym_info_t sym;
    char name[255];
    char file[MAXIMUM_PATH];
    module_data_t *data;
    data = dr_lookup_module(addr);
    if (data == NULL) {
        dr_fprintf(f, "%s " PFX " ? ??:0\n", prefix, addr);
        return;
    }
    sym.struct_size = sizeof(sym);
    sym.name = name;
    sym.name_size = 254;
    sym.file = file;
    sym.file_size = MAXIMUM_PATH;
    symres = drsym_lookup_address(data->full_path, addr - data->start, &sym,
                                  DRSYM_DEFAULT_FLAGS);
    if (symres == DRSYM_SUCCESS || symres == DRSYM_ERROR_LINE_NOT_AVAILABLE) {
        const char *modname = dr_module_preferred_name(data);
        if (modname == NULL)
            modname = "<noname>";
        dr_fprintf(f, "%s " PFX " %s!%s+" PIFX, prefix, addr, modname, sym.name,
                   addr - data->start - sym.start_offs);
        if (symres == DRSYM_ERROR_LINE_NOT_AVAILABLE) {
            dr_fprintf(f, " ??:0\n");
        } else {
            dr_fprintf(f, " %s:%" UINT64_FORMAT_CODE "+" PIFX "\n", sym.file,
sym.line, sym.line_offs);
        }
    } else
        dr_fprintf(f, "%s " PFX " ? ??:0\n", prefix, addr);
    dr_free_module_data(data);
}
*/

#ifndef VAMOS_SOURCES_CODEGEN_NEW_TRACE_H_
#define VAMOS_SOURCES_CODEGEN_NEW_TRACE_H_

#include "trace.h"
#include "htrace.h"

static size_t nextTraceID = 0;
static size_t nextHTraceID = 0;

template <typename TraceType>
TraceBase *__new_trace() {
    return new TraceType(++nextTraceID);
}

template <typename HTraceType>
HTrace *__new_hyper_trace() {
    return new HTraceType(++nextHTraceID);
}

#endif

#ifndef VAMOS_SOURCES_CODEGEN_NEW_TRACE_H_
#define VAMOS_SOURCES_CODEGEN_NEW_TRACE_H_

#include "trace.h"

static size_t nextTraceID = 0;

template <typename TraceType>
TraceBase *__new_trace() {
    return new TraceType(++nextTraceID);
}

#endif

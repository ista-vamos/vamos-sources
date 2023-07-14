#ifndef VAMOS_SOURCES_CODEGEN_NEW_TRACE_H_
#define VAMOS_SOURCES_CODEGEN_NEW_TRACE_H_

#include "trace.h"

template <typename... Types>
TraceBase *__new_trace() {
    constexpr size_t ev_size = sizeof...(Types);
    return nullptr;
}

#endif

#ifndef VAMOS_STDOUT_TRACE_H
#define VAMOS_STDOUT_TRACE_H

#include <cassert>
#include <iostream>

#include "trace.h"

///
// A template for easier creation of generated trace classes
//
template <typename EventTy>
class StdoutTrace : public TraceBase {
    vms_eventid _event_id{0};

   public:
    StdoutTrace(size_t id, size_t type) : TraceBase(id, type) {}

    void push(const EventTy &e) {
        ++_event_id;
        std::cout << "[" << id() << "]: " << EventAndID<EventTy>(e, _event_id)
                  << "\n";
    };
    void push(const EventTy *e) { push(*e); }

    size_t size() const { return _event_id; }
};

#endif

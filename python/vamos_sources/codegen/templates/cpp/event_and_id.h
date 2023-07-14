#ifndef VAMOS_EVENT_AND_ID_H
#define VAMOS_EVENT_AND_ID_H

#include <vamos-buffers/cpp/event.h>

template <typename EventTy>
struct EventAndID {
    const EventTy& event;
    vms_eventid id;

    explicit EventAndID(const EventTy& ev, vms_eventid id) : event(ev), id(id) {}
};

#endif

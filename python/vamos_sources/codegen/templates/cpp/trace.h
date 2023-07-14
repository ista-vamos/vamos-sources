#ifndef VAMOS_TRACE_H
#define VAMOS_TRACE_H

#include <cstring>
#include <vector>

#include <vamos-buffers/cpp/event.h>

using vamos::Event;

///
// Base class for traces
class TraceBase {
  const size_t _id;
  // the type of trace is a number that uniquely identifies its child class type
  // that is generated
  const size_t _type;
  bool _done{false};

public:
  TraceBase(size_t id, size_t type) : _id(id), _type(type) {}
  void setDone() { _done = true; }

  bool done() const { return _done; }
  size_t id() const { return _id; }
  size_t type() const { return _type; }
};



///
// A template for easier creation of generated trace classes
//
template <typename EventTy> class Trace : public TraceBase {
  std::vector<EventTy> _events;

public:
  Trace(size_t id, size_t type) : TraceBase(id, type) {}

  void append(const EventTy *e) { _events.push_back(*e); }
  void append(const EventTy &e) { _events.push_back(e); };

  Event *get(size_t idx) { return &_events[idx]; }
  const Event *get(size_t idx) const { return &_events[idx]; }

  Event *try_get(size_t idx) {
    if (idx < _events.size())
        return &_events[idx];
     return nullptr;
  }
  const Event *try_get(size_t idx) const {
    if (idx < _events.size())
        return &_events[idx];
     return nullptr;
  }

  Event *operator[](size_t idx) { return get(idx); }
  const Event *operator[](size_t idx) const { return get(idx); }

  size_t size() const { return _events.size(); }
};

#endif

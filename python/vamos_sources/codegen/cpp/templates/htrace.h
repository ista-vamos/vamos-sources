#ifndef VAMOS_HTRACE_H
#define VAMOS_HTRACE_H

#include <cassert>
#include <cstring>
#include <vector>

#include <vamos-buffers/cpp/event.h>

using vamos::Event;

class TraceBase;

///
// Base class for traces
class HTraceBase {
  const size_t _id;
  // the type of trace is a number that uniquely identifies its child class type
  // that is generated
  const size_t _type;
  bool _done{false};

public:
  HTraceBase(size_t id, size_t type) : _id(id), _type(type) {}
  void setDone() { _done = true; }

  bool done() const { return _done; }
  size_t id() const { return _id; }
  size_t type() const { return _type; }
};


class HTrace : public HTraceBase {
  std::vector<TraceBase *> _traces;

public:
  HTrace(size_t id, size_t type) : HTraceBase(id, type) {}

  size_t size() const { return _traces.size(); }
};


#endif

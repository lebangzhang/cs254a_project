// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <functional>
#include <iostream>
#include <memory>
#include <vector>
#include <list>
#include <queue>
#include <assert.h>
#include "mempool.h"
#include "util.h"
#include "linked_list.h"

namespace vortex {

class SimObjectBase;

class SimPortBase {
public:
  virtual ~SimPortBase() {}

  SimObjectBase* module() const {
    return module_;
  }

protected:
  SimPortBase(SimObjectBase* module): module_(module) {}

  virtual void do_pop() = 0;

  SimPortBase& operator=(const SimPortBase&) = delete;

  SimObjectBase* module_;

  LinkedListNode<SimPortBase> pop_list_;
  LinkedListNode<SimPortBase> push_list_;

  friend class SimPlatform;
};

///////////////////////////////////////////////////////////////////////////////

template <typename Pkt>
class SimPort : public SimPortBase {
public:
  typedef std::function<void (const Pkt&, uint64_t)> TxCallback;

  SimPort(SimObjectBase* module, uint32_t capacity = 0)
    : SimPortBase(module)
    , capacity_(capacity)
    , sink_(nullptr)
    , tx_cb_(nullptr)
    , chained_(false)
  {}

  void bind(SimPort<Pkt>* sink) {
    assert(sink_ == nullptr);
    sink->chained_ = true;
    sink_ = sink;
  }

  void unbind() {
    if (sink_) {
      sink_->chained_ = false;
      sink_ = nullptr;
    }
  }

  bool connected() const {
    return (sink_ != nullptr);
  }

  SimPort* sink() const {
    return sink_;
  }

  bool empty() const {
    return queue_.empty();
  }

  bool full() const {
    return (capacity_ != 0 && queue_.size() >= capacity_);
  }

  uint32_t size() const {
    return queue_.size();
  }

  uint32_t capacity() const {
    return capacity_;
  }

  const Pkt& front() const {
    return queue_.front();
  }

  Pkt& front() {
    return queue_.front().pkt;
  }

  void push(const Pkt& pkt, uint64_t delay = 1) {
    __assert(!chained_, "cannot execute push on a chained port!")
    this->do_push(pkt, delay);
  }

  uint64_t pop();

  void tx_callback(const TxCallback& callback) {
    tx_cb_ = callback;
  }

  uint64_t arrival_time() const {
    if (queue_.empty())
      return 0;
    return queue_.front().cycles;
  }

protected:
  struct timed_pkt_t {
    Pkt      pkt;
    uint64_t cycles;
  };

  std::queue<timed_pkt_t> queue_;
  uint32_t   capacity_;
  SimPort*   sink_;
  TxCallback tx_cb_;
  bool chained_;

  void do_pop() override {
    queue_.pop();
  }

  void do_push(const Pkt& pkt, uint64_t delay);

  void transfer(const Pkt& data, uint64_t cycles) {
    if (tx_cb_) {
      tx_cb_(data, cycles);
    }
    if (sink_) {
      sink_->transfer(data, cycles);
    } else {
      queue_.push({data, cycles});
    }
  }

  SimPort& operator=(const SimPort&) = delete;

  template <typename U> friend class SimPortEvent;
};

///////////////////////////////////////////////////////////////////////////////

class SimEventBase {
public:
  typedef std::shared_ptr<SimEventBase> Ptr;

  virtual ~SimEventBase() {}

  virtual void fire() const = 0;

  uint64_t cycles() const {
    return cycles_;
  }

protected:
  SimEventBase(uint64_t cycles) : cycles_(cycles) {}

  uint64_t cycles_;
};

///////////////////////////////////////////////////////////////////////////////

template <typename Pkt>
class SimCallEvent : public SimEventBase {
public:
  void fire() const override {
    func_(pkt_);
  }

  typedef std::function<void (const Pkt&)> Func;

  SimCallEvent(const Func& func, const Pkt& pkt, uint64_t cycles)
    : SimEventBase(cycles)
    , func_(func)
    , pkt_(pkt)
  {}

protected:
  Func func_;
  Pkt  pkt_;
};

///////////////////////////////////////////////////////////////////////////////

template <typename Pkt>
class SimPortEvent : public SimEventBase {
public:
  void fire() const override {
    const_cast<SimPort<Pkt>*>(port_)->transfer(pkt_, cycles_);
  }

  SimPortEvent(const SimPort<Pkt>* port, const Pkt& pkt, uint64_t cycles)
    : SimEventBase(cycles)
    , port_(port)
    , pkt_(pkt)
  {}

protected:
  const SimPort<Pkt>* port_;
  Pkt pkt_;
};

///////////////////////////////////////////////////////////////////////////////

class SimContext {
private:
  SimContext() {}

  friend class SimPlatform;
};

///////////////////////////////////////////////////////////////////////////////

class SimObjectBase {
public:
  typedef std::shared_ptr<SimObjectBase> Ptr;

  virtual ~SimObjectBase() {}

  const std::string& name() const {
    return name_;
  }

protected:

  SimObjectBase(const SimContext&, const std::string& name) : name_(name) {}

private:

  std::string name_;

  virtual void do_reset() = 0;

  virtual void do_tick() = 0;

  friend class SimPortBase;
  friend class SimPlatform;
};

///////////////////////////////////////////////////////////////////////////////

template <typename Impl>
class SimObject : public SimObjectBase {
public:
  typedef std::shared_ptr<Impl> Ptr;

  template <typename... Args>
  static Ptr Create(Args&&... args);

protected:

  SimObject(const SimContext& ctx, const std::string& name)
    : SimObjectBase(ctx, name)
  {}

private:

  const Impl* impl() const {
    return static_cast<const Impl*>(this);
  }

  Impl* impl() {
    return static_cast<Impl*>(this);
  }

  void do_reset() override {
    this->impl()->reset();
  }

  void do_tick() override {
    this->impl()->tick();
  }
};

///////////////////////////////////////////////////////////////////////////////

class SimPlatform {
public:
  static SimPlatform& instance() {
    static SimPlatform s_inst;
    return s_inst;
  }

  bool initialize() {
    //--
    return true;
  }

  void finalize() {
    instance().clear();
  }

  template <typename Impl, typename... Args>
  typename SimObject<Impl>::Ptr create_object(Args&&... args) {
    auto obj = std::make_shared<Impl>(SimContext{}, std::forward<Args>(args)...);
    objects_.push_back(obj);
    return obj;
  }

  void release_object(const SimObjectBase::Ptr& object) {
    objects_.remove(object);
  }

  template <typename Pkt>
  void schedule(const typename SimCallEvent<Pkt>::Func& callback,
                const Pkt& pkt,
                uint64_t delay) {
    assert(delay != 0);
    static PoolAllocator<SimCallEvent<Pkt>, 64> s_allocator;
    auto evt = std::allocate_shared<SimCallEvent<Pkt>>(s_allocator, callback, pkt, cycles_ + delay);
    events_.emplace_back(evt);
  }

  void reset() {
    events_.clear();
    for (auto& object : objects_) {
      object->do_reset();
    }
    cycles_ = 0;
  }

  void tick() {
    //printf("*** tick: %lu\n", cycles_);
    // fire events
    auto evt_it = events_.begin();
    auto evt_it_end = events_.end();
    while (evt_it != evt_it_end) {
      auto& event = *evt_it;
      if (cycles_ >= event->cycles()) {
        event->fire();
        evt_it = events_.erase(evt_it);
      } else {
        ++evt_it;
      }
    }
    // execute objects
    for (auto object : objects_) {
      object->do_tick();
    }
    // realize objects
    for (auto it = pop_list_.begin(); it != pop_list_.end();) {
      it->do_pop();
      it = pop_list_.erase(it);
    }
    push_list_.clear();
    // advance clock
    ++cycles_;
  }

  uint64_t cycles() const {
    return cycles_;
  }

private:

  SimPlatform() : cycles_(0) {}

  virtual ~SimPlatform() {
    this->clear();
  }

  void clear() {
    objects_.clear();
    events_.clear();
  }

  template <typename Pkt>
  void schedule_push(SimPort<Pkt>* port, const Pkt& pkt, uint64_t delay) {
    //printf("*** schedule_push: %s::%p\n", port->module()->name().c_str(), port);
    assert(delay != 0);
    if (port->capacity() != 0) {
      __assert(0 == push_list_.count(port), "cannot enqueue a port multiple times during the same cycle!");
      push_list_.push_back(port);
    }

    // schedule update event
    static PoolAllocator<SimPortEvent<Pkt>, 64> s_allocator;
    auto evt = std::allocate_shared<SimPortEvent<Pkt>>(s_allocator, port, pkt, cycles_ + delay);
    events_.emplace_back(evt);
  }

  template <typename Pkt>
  void schedule_pop(SimPort<Pkt>* port) {
    //printf("*** schedule_pop: %s::%p\n", port->module()->name().c_str(), port);
    __assert(0 == pop_list_.count(port), "cannot dequeue a port multiple times during the same cycle!");
    pop_list_.push_back(port);
  }

  std::list<SimObjectBase::Ptr> objects_;
  std::list<SimEventBase::Ptr> events_;
  LinkedList<SimPortBase, &SimPortBase::push_list_> push_list_;
  LinkedList<SimPortBase, &SimPortBase::pop_list_> pop_list_;
  uint64_t cycles_;

  template <typename U> friend class SimPort;
};

///////////////////////////////////////////////////////////////////////////////

template <typename Pkt>
void SimPort<Pkt>::do_push(const Pkt& pkt, uint64_t delay) {
  if (sink_ && !tx_cb_) {
    sink_->do_push(pkt, delay);
  } else {
    SimPlatform::instance().schedule_push(this, pkt, delay);
  }
}

template <typename Pkt>
uint64_t SimPort<Pkt>::pop() {
  SimPlatform::instance().schedule_pop(this);
  return queue_.front().cycles;
}

///////////////////////////////////////////////////////////////////////////////

template <typename Impl>
template <typename... Args>
typename SimObject<Impl>::Ptr SimObject<Impl>::Create(Args&&... args) {
  return SimPlatform::instance().create_object<Impl>(std::forward<Args>(args)...);
}

}
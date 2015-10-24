// MessageQueue is a thread-safe queue of messages.

// Copyright (c) 2015 C. Scott Ananian <cscott@cscott.net>
#ifndef NODE_PHP_EMBED_MESSAGEQUEUE_H_
#define NODE_PHP_EMBED_MESSAGEQUEUE_H_

#include <cassert>
#include <list>

#include "nan.h"

namespace node_php_embed {

class Message;

// A queue of messages passed between threads.
class MessageQueue {
 public:
  explicit MessageQueue(uv_async_t *async) : async_(async), data_() {
    uv_mutex_init(&lock_);
    uv_cond_init(&cond_);
  }
  virtual ~MessageQueue() {
    uv_cond_destroy(&cond_);
    uv_mutex_destroy(&lock_);
  }
  inline uv_async_t *async() { return async_; }
  void Push(Message *m) {
    assert(m);
    _Push(m);
  }
  void Notify() {
    _Push(NULL);
  }
  // Processes all messages on the queue.
  // Returns true if at least one message was processed.
  // If `match` is non null, it will block if the queue is empty and
  // continue processing messages until `match->IsProcessed` is true.
  template<typename Func>
  bool DoProcess(Message *match, Func func) {
    bool sawOne = false, loop = true;
    Message *m;

    while (loop) {
      // Grab one message at a time, so that we don't end up processing
      // messages out of order in case `func(m)` below ends up creating
      // a recursive processing loop.
      uv_mutex_lock(&lock_);
      if (data_.empty()) {
        m = NULL;
        if (match) {
          // We're blocking for a particular message, and there's nothing here.
          // Block to wait for some data.
          uv_cond_wait(&cond_, &lock_);
        } else {
          loop = false;
        }
      } else {
        sawOne = true;
        m = data_.front();
        data_.pop_front();
      }
      uv_mutex_unlock(&lock_);

      if (m) { func(m); }
      // Check whether either we processed the matching message,
      // or else a recursive processing loop handled it for us.
      if (match && match->IsProcessed()) { loop = false; }
    }
    return sawOne;
  }
  uv_async_t *ClearAsync() {
    uv_async_t *a = async_;
    async_ = NULL;
    return a;
  }

 private:
  void _Push(Message *m) {
    uv_mutex_lock(&lock_);
    if (m) { data_.push_back(m); }
    uv_cond_broadcast(&cond_);
    uv_mutex_unlock(&lock_);
    if (async_) {
      uv_async_send(async_);
    }
  }
  uv_async_t *async_;
  uv_mutex_t lock_;
  uv_cond_t cond_;
  std::list<Message *> data_;
};

}  // namespace node_php_embed

#endif  //  NODE_PHP_EMBED_MESSAGEQUEUE_H_

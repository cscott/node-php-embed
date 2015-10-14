#ifndef ASYNCLOCKWORKER_H
#define ASYNCLOCKWORKER_H
#include <nan.h>
#include <list>

namespace node_php_embed {

class barrier_pair {
 public:
  explicit barrier_pair() : start(), finish() {
    uv_barrier_init(&start, 2);
    uv_barrier_init(&finish, 2);
  }
  virtual ~barrier_pair() {
    uv_barrier_destroy(&start);
    uv_barrier_destroy(&finish);
  }
  uv_barrier_t start, finish;
};

/* This class is similar to Nan's AsyncProgressWorker, except that
 * we guarantee not to lose/discard messages sent from the worker.
 */
/* abstract */ class AsyncLockWorker : public Nan::AsyncWorker {
 public:
  explicit AsyncLockWorker(Nan::Callback *callback_, v8::Isolate *isolate) : AsyncWorker(callback_), asyncdata_(), waitingForLock(false), isolate_(isolate) {
    async = new uv_async_t;
    uv_async_init(uv_default_loop(), async, AsyncLock_);
    async->data = this;

    uv_mutex_init(&async_lock);
  }

  virtual ~AsyncLockWorker() {
    uv_mutex_destroy(&async_lock);
    // can't safely delete entries from asyncdata_, it better be empty.
  }

  void WorkComplete() {
    uv_mutex_lock(&async_lock);
    waitingForLock = !asyncdata_.empty();
    uv_mutex_unlock(&async_lock);

    if (!waitingForLock) {
      Nan::AsyncWorker::WorkComplete();
      ReallyDestroy();
    } else {
      // Queue another trip through WorkQueue
      uv_async_send(async);
    }
  }

  void WorkQueue() {
    std::list<barrier_pair *> newData;
    bool waiting;

    uv_mutex_lock(&async_lock);
    newData.splice(newData.begin(), asyncdata_);
    waiting = waitingForLock;
    uv_mutex_unlock(&async_lock);

    // Temporarily leave this isolate while we process the work list.
    {
      isolate_->Exit();
      v8::Unlocker unlocker(isolate_);

      for (std::list<barrier_pair *>::iterator it = newData.begin();
           it != newData.end(); it++) {
        barrier_pair *b = *it;
        uv_barrier_wait(&(b->start));
        // computation in the other thread happens here.
        uv_barrier_wait(&(b->finish));
      }
    }
    // Reclaim this isolate.
    isolate_->Enter();

    // If we were waiting for the stream to empty, perhaps it's time to
    // invoke WorkComplete.
    if (waiting) {
      WorkComplete();
    }
  }

  class ExecutionLockRequest {
    friend class AsyncLockWorker;
  public:
    void Send(barrier_pair *b) const {
      that_->SendLock_(b);
    }
    v8::Isolate *GetIsolate() const {
      return that_->isolate_;
    }
    AsyncLockWorker *GetWorker() const {
      return that_;
    }
  private:
    explicit ExecutionLockRequest(AsyncLockWorker* that) : that_(that) {}
    NAN_DISALLOW_ASSIGN_COPY_MOVE(ExecutionLockRequest)
    AsyncLockWorker* const that_;
  };

  virtual void Execute(const ExecutionLockRequest& execLockRequest) = 0;

  virtual void ReallyDestroy() {
    // The AsyncClose_ method handles deleting `this`.
    uv_close(reinterpret_cast<uv_handle_t*>(async), AsyncClose_);
  }
  virtual void Destroy() {
    /* do nothing -- we're going to trigger ReallyDestroy from
     * WorkComplete */
  }

 private:
  void Execute() /*final override*/ {
    ExecutionLockRequest execLockRequest(this);
    Execute(execLockRequest);
  }

  void SendLock_(barrier_pair *b) {
    uv_mutex_lock(&async_lock);
    asyncdata_.push_back(b);
    uv_mutex_unlock(&async_lock);

    uv_async_send(async);
  }

  NAN_INLINE static NAUV_WORK_CB(AsyncLock_) {
    AsyncLockWorker *worker =
      static_cast<AsyncLockWorker*>(async->data);
    worker->WorkQueue();
  }

  NAN_INLINE static void AsyncClose_(uv_handle_t* handle) {
    AsyncLockWorker *worker =
      static_cast<AsyncLockWorker*>(handle->data);
    delete reinterpret_cast<uv_async_t*>(handle);
    delete worker;
  }

  uv_async_t *async;
  uv_mutex_t async_lock;
  std::list<barrier_pair *> asyncdata_;
  bool waitingForLock;
  v8::Isolate *isolate_;
};

class BarrierWait {
 public:
  explicit BarrierWait(const AsyncLockWorker::ExecutionLockRequest *execLockRequest) : b_() {
    execLockRequest->Send(&b_);
    uv_barrier_wait(&(b_.start));
  }
  virtual ~BarrierWait() {
    uv_barrier_wait(&(b_.finish));
  }
 private:
  barrier_pair b_;
};

/* Stack-allocated class which queues a barrier_pair, waits until it's safe to
 * enter an isolate, then enters it. */
class WaitForNode {
 public:
  explicit WaitForNode(const AsyncLockWorker::ExecutionLockRequest *elr) :
    wait(elr),
    locker(elr->GetIsolate()),
      isolate_scope(elr->GetIsolate()) {}
    virtual ~WaitForNode() {}
 private:
  BarrierWait wait;
  v8::Locker locker;
  v8::Isolate::Scope isolate_scope;
};

}
#endif

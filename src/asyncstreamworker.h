#ifndef ASYNCSTREAMWORKER_H
#define ASYNCSTREAMWORKER_H
#include <nan.h>
#include <list>

using namespace Nan;

namespace node_php_embed {

/* This class is similar to Nan's AsyncProgressWorker, except that
 * we guarantee not to lose/discard messages sent from the worker.
 */
/* abstract */ class AsyncStreamWorker : public Nan::AsyncWorker {
 public:
  explicit AsyncStreamWorker(Callback *callback_)
      : AsyncWorker(callback_), asyncdata_(), waitingForStream(false) {
    async = new uv_async_t;
    uv_async_init(
        uv_default_loop()
      , async
      , AsyncStream_
    );
    async->data = this;

    uv_mutex_init(&async_lock);
  }

  virtual ~AsyncStreamWorker() {
    uv_mutex_destroy(&async_lock);

    for (std::list<std::pair<char*,size_t>>::iterator it = asyncdata_.begin();
         it != asyncdata_.end(); it++) {
        delete[] it->first;
    }
    asyncdata_.clear();
  }

  void WorkComplete() {
    uv_mutex_lock(&async_lock);
    waitingForStream = !asyncdata_.empty();
    uv_mutex_unlock(&async_lock);

    if (!waitingForStream) {
        Nan::AsyncWorker::WorkComplete();
        ReallyDestroy();
    } else {
        // Queue another trip through WorkStream
        uv_async_send(async);
    }
  }

  void WorkStream() {
    std::list<std::pair<char*,size_t>> newData;
    bool waiting;

    uv_mutex_lock(&async_lock);
    newData.splice(newData.begin(), asyncdata_);
    waiting = waitingForStream;
    uv_mutex_unlock(&async_lock);

    for (std::list<std::pair<char*,size_t>>::iterator it = newData.begin();
         it != newData.end(); it++) {
        HandleStreamCallback(it->first, it->second);
        delete[] it->first;
    }

    // If we were waiting for the stream to empty, perhaps it's time to
    // invoke WorkComplete.
    if (waiting) {
        WorkComplete();
    }
  }

  class ExecutionStream {
    friend class AsyncStreamWorker;
   public:
    // You could do fancy generics with templates here.
    void Send(const char* data, size_t size) const {
        that_->SendStream_(data, size);
    }

   private:
    explicit ExecutionStream(AsyncStreamWorker* that) : that_(that) {}
    NAN_DISALLOW_ASSIGN_COPY_MOVE(ExecutionStream)
    AsyncStreamWorker* const that_;
  };

  virtual void Execute(const ExecutionStream& stream) = 0;
  virtual void HandleStreamCallback(const char *data, size_t size) = 0;

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
      ExecutionStream stream(this);
      Execute(stream);
  }

  void SendStream_(const char *data, size_t size) {
    char *new_data = new char[size];
    memcpy(new_data, data, size);

    uv_mutex_lock(&async_lock);
    asyncdata_.push_back(std::pair<char*,size_t>(new_data, size));
    uv_mutex_unlock(&async_lock);

    uv_async_send(async);
  }

  NAN_INLINE static NAUV_WORK_CB(AsyncStream_) {
    AsyncStreamWorker *worker =
            static_cast<AsyncStreamWorker*>(async->data);
    worker->WorkStream();
  }

  NAN_INLINE static void AsyncClose_(uv_handle_t* handle) {
    AsyncStreamWorker *worker =
            static_cast<AsyncStreamWorker*>(handle->data);
    delete reinterpret_cast<uv_async_t*>(handle);
    delete worker;
  }

  uv_async_t *async;
  uv_mutex_t async_lock;
  std::list<std::pair<char*,size_t>> asyncdata_;
  bool waitingForStream;
};

}
#endif

#ifndef ASYNCSTREAMWORKER_H
#define ASYNCSTREAMWORKER_H
#include <nan.h>

using namespace Nan;

namespace node_php_embed {

/* This class is similar to Nan's AsyncProgressWorker, except that
 * we guarantee not to lose/discard messages sent from the worker.
 */
/* abstract */ class AsyncStreamWorker : public Nan::AsyncWorker {
 public:
  explicit AsyncStreamWorker(Callback *callback_)
      : AsyncWorker(callback_), asyncdata_(NULL), asyncsize_(0) {
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

    if (asyncdata_) {
      delete[] asyncdata_;
    }
  }

  void WorkStream() {
    uv_mutex_lock(&async_lock);
    char *data = asyncdata_;
    size_t size = asyncsize_;
    asyncdata_ = NULL;
    uv_mutex_unlock(&async_lock);

    // Dont send stream events after we've already completed.
    if (callback) {
        HandleStreamCallback(data, size);
    }
    delete[] data;
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

  virtual void Destroy() {
      uv_close(reinterpret_cast<uv_handle_t*>(async), AsyncClose_);
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
    char *old_data = asyncdata_;
    asyncdata_ = new_data;
    asyncsize_ = size;
    uv_mutex_unlock(&async_lock);

    if (old_data) {
      delete[] old_data;
    }
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
  char *asyncdata_;
  size_t asyncsize_;
};

}
#endif

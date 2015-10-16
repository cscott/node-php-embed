#ifndef ASYNCMESSAGEWORKER_H
#define ASYNCMESSAGEWORKER_H
#include <nan.h>
#include <list>
#include "messages.h"

namespace node_php_embed {

/* This class is similar to Nan's AsyncProgressWorker, except that
 * we guarantee not to lose/discard messages sent from the worker.
 */
/* abstract */ class AsyncMessageWorker : public Nan::AsyncWorker {
 public:
  explicit AsyncMessageWorker(Nan::Callback *callback_)
      : AsyncWorker(callback_), asyncdata_(), waitingForLock_(false),
        nextId_(0) {
    async = new uv_async_t;
    uv_async_init(uv_default_loop(), async, AsyncMessage_);
    async->data = this;

    uv_mutex_init(&async_lock);

    objToId_.Reset(v8::NativeWeakMap::New(v8::Isolate::GetCurrent()));
  }

  virtual ~AsyncMessageWorker() {
    uv_mutex_destroy(&async_lock);
    // can't safely delete entries from asyncdata_, it better be empty.
    objToId_.Reset();
  }

  // Map Js object to an index
  uint32_t IdForJsObj(const v8::Local<v8::Object> o) {
      // Have we already mapped this?
      Nan::HandleScope scope;
      v8::Local<v8::NativeWeakMap> objToId = Nan::New(objToId_);
      if (objToId->Has(o)) {
          return Nan::To<uint32_t>(objToId->Get(o)).FromJust();
      }
      uint32_t r = (nextId_++);
      objToId->Set(o, Nan::New(r));
      SaveToPersistent(r, o);
      return r;
  }

  void WorkComplete() {
    uv_mutex_lock(&async_lock);
    waitingForLock_ = !asyncdata_.empty();
    uv_mutex_unlock(&async_lock);

    if (!waitingForLock_) {
      Nan::AsyncWorker::WorkComplete();
      ReallyDestroy();
    } else {
      // Queue another trip through WorkQueue
      uv_async_send(async);
    }
  }

  // This is in the Js Thread; dispatch requests from PHP thread.
  void WorkQueue() {
    std::list<MessageToJs *> newData;
    bool waiting;

    uv_mutex_lock(&async_lock);
    newData.splice(newData.begin(), asyncdata_);
    waiting = waitingForLock_;
    uv_mutex_unlock(&async_lock);

    for (std::list<MessageToJs *>::iterator it = newData.begin();
         it != newData.end(); it++) {
       MessageToJs *m = *it;
       // do computation in Js Thread
       m->ExecuteJs();
       // PHP thread is woken at end of executeJs()
    }

    // If we were waiting for the stream to empty, perhaps it's time to
    // invoke WorkComplete.
    if (waiting) {
      WorkComplete();
    }
  }

  class MessageChannel : public JsMessageChannel {
    friend class AsyncMessageWorker;
  public:
    virtual void Send(MessageToJs *m) const {
      that_->SendMessage_(m);
    }
    AsyncMessageWorker *GetWorker() const {
      return that_;
    }
    virtual uint32_t IdForJsObj(const v8::Local<v8::Object> o) {
        return that_->IdForJsObj(o);
    }
    virtual v8::Local<v8::Object> GetJs(uint32_t id) {
        Nan::EscapableHandleScope scope;
        return scope.Escape(Nan::To<v8::Object>(
            that_->GetFromPersistent(id)
        ).ToLocalChecked());
    }
    virtual uint32_t IdForPhpObj(const zval *o) {
        return 0; // XXX
    }
    virtual void GetPhp(uint32_t id, zval *return_value TSRMLS_DC) {
        // XXX use GetFromPersistent, and then unwrap v8::External?
    }
  private:
    explicit MessageChannel(AsyncMessageWorker* that) : that_(that) {}
    NAN_DISALLOW_ASSIGN_COPY_MOVE(MessageChannel)
    AsyncMessageWorker* const that_;
  };

  virtual void Execute(MessageChannel& messageChannel) = 0;

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
    MessageChannel messageChannel(this);
    Execute(messageChannel);
  }

  void SendMessage_(MessageToJs *b) {
    uv_mutex_lock(&async_lock);
    asyncdata_.push_back(b);
    uv_mutex_unlock(&async_lock);

    uv_async_send(async);
  }

  NAN_INLINE static NAUV_WORK_CB(AsyncMessage_) {
    AsyncMessageWorker *worker =
      static_cast<AsyncMessageWorker*>(async->data);
    worker->WorkQueue();
  }

  NAN_INLINE static void AsyncClose_(uv_handle_t* handle) {
    AsyncMessageWorker *worker =
      static_cast<AsyncMessageWorker*>(handle->data);
    delete reinterpret_cast<uv_async_t*>(handle);
    delete worker;
  }

  uv_async_t *async;
  uv_mutex_t async_lock;
  std::list<MessageToJs *> asyncdata_;
  bool waitingForLock_;
  // Js Object mapping (along with GetFromPersistent/etc)
  Nan::Persistent<v8::NativeWeakMap> objToId_;
  uint32_t nextId_;
};

}
#endif

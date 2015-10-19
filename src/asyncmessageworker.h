#ifndef ASYNCMESSAGEWORKER_H
#define ASYNCMESSAGEWORKER_H
#include <nan.h>
#include <cassert>
#include <list>
#include <unordered_map>
#include "messages.h"
#include "node_php_jsobject_class.h"

namespace node_php_embed {

/* This class is similar to Nan's AsyncProgressWorker, except that
 * we guarantee not to lose/discard messages sent from the worker.
 */
/* abstract */ class AsyncMessageWorker : public Nan::AsyncWorker {
 public:
  class MessageChannel;
  explicit AsyncMessageWorker(Nan::Callback *callback)
      : AsyncWorker(callback), asyncdata_(), waitingForLock_(false),
        phpObjToId_(), phpObjList_(),
        // id #0 is reserved for "invalid object"
        nextId_(1) {
    async = new uv_async_t;
    uv_async_init(uv_default_loop(), async, AsyncMessage_);
    async->data = this;

    uv_mutex_init(&async_lock_);
    uv_mutex_init(&id_lock_);

    jsObjToId_.Reset(v8::NativeWeakMap::New(v8::Isolate::GetCurrent()));
  }

  virtual ~AsyncMessageWorker() {
    // invalidate any lingering js wrapper objects to this request
    ClearAllJsIds();
    uv_mutex_destroy(&async_lock_);
    uv_mutex_destroy(&id_lock_);
    // can't safely delete entries from asyncdata_, it better be empty.
    jsObjToId_.Reset();
  }

  // Map Js object to an index (JS thread only)
  objid_t IdForJsObj(const v8::Local<v8::Object> o) {
      // Have we already mapped this?
      Nan::HandleScope scope;
      v8::Local<v8::NativeWeakMap> jsObjToId = Nan::New(jsObjToId_);
      if (jsObjToId->Has(o)) {
          return Nan::To<objid_t>(jsObjToId->Get(o)).FromJust();
      }
      uv_mutex_lock(&id_lock_);
      objid_t id = (nextId_++);
      uv_mutex_unlock(&id_lock_);
      jsObjToId->Set(o, Nan::New(id));
      SaveToPersistent(id, o);
      return id;
  }
  v8::Local<v8::Object> JsObjForId(objid_t id) {
      Nan::EscapableHandleScope scope;
      // XXX if doesn't exist, then make a wrapper (and store it in the maps)
      return scope.Escape(Nan::To<v8::Object>(
        GetFromPersistent(id)
      ).ToLocalChecked());
  }
  // Map PHP object to an index (PHP thread only)
  objid_t IdForPhpObj(zval *z) {
      assert(Z_TYPE_P(z) == IS_OBJECT);
      zend_object_handle handle = Z_OBJ_HANDLE_P(z);
      if (phpObjToId_.count(handle)) {
          return phpObjToId_.at(handle);
      }
      uv_mutex_lock(&id_lock_);
      objid_t id = (nextId_++);
      uv_mutex_unlock(&id_lock_);
      if (id >= phpObjList_.size()) { phpObjList_.resize(id+1); }
      // xxx clone/separate z?
      Z_ADDREF_P(z);
      phpObjList_[id] = z;
      phpObjToId_[handle] = id;
      return id;
  }
  // returned value is owned by objectmapper, caller should not release it.
  zval * PhpObjForId(MessageChannel *channel, objid_t id TSRMLS_DC) {
      if (id >= phpObjList_.size()) { phpObjList_.resize(id+1); }
      ZVal z(phpObjList_[id] ZEND_FILE_LINE_CC);
      if (z.IsNull()) {
          node_php_jsobject_create(z.Ptr(), channel, id TSRMLS_CC);
          phpObjList_[id] = z.Ptr();
          phpObjToId_[Z_OBJ_HANDLE_P(z.Ptr())] = id;
          // one excess reference: owned by objectmapper
          return z.Escape();
      }
      // don't increment reference
      return z.Ptr();
  }
  // Free PHP references associated with an id (from PHP thread)
  void ClearPhpId(objid_t id) {
      zval *z = (id < phpObjList_.size()) ? phpObjList_[id] : NULL;
      if (z) {
          phpObjList_[id] = NULL;
          phpObjToId_.erase(Z_OBJ_HANDLE_P(z));
          zval_ptr_dtor(&z);
      }
  }
  void ClearAllPhpIds() {
      for (objid_t id = 1; id < nextId_; id++) {
          ClearPhpId(id);
      }
  }
  // Free JS references associated with an id (from JS thread)
  void ClearJsId(objid_t id) {
      Nan::HandleScope scope;
      Nan::MaybeLocal<v8::Object> o =
          Nan::To<v8::Object>(GetFromPersistent(id));
      if (o.IsEmpty()) { return; }
      // XXX depending on o's type, set its "is request valid" flag to false
      // since there might be other references to this object.
      v8::Local<v8::NativeWeakMap> jsObjToId = Nan::New(jsObjToId_);
      jsObjToId->Delete(o.ToLocalChecked());
      SaveToPersistent(id, Nan::Undefined());
  }
  void ClearAllJsIds() {
      for (objid_t id = 1; id < nextId_; id++) {
          ClearJsId(id);
      }
  }

  void WorkComplete() {
    uv_mutex_lock(&async_lock_);
    waitingForLock_ = !asyncdata_.empty();
    uv_mutex_unlock(&async_lock_);

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

    uv_mutex_lock(&async_lock_);
    newData.splice(newData.begin(), asyncdata_);
    waiting = waitingForLock_;
    uv_mutex_unlock(&async_lock_);

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
    virtual objid_t IdForJsObj(const v8::Local<v8::Object> o) {
        return that_->IdForJsObj(o);
    }
    virtual v8::Local<v8::Object> JsObjForId(objid_t id) {
        return that_->JsObjForId(id);
    }
    virtual objid_t IdForPhpObj(zval *o) {
        return that_->IdForPhpObj(o);
    }
    virtual zval * PhpObjForId(objid_t id TSRMLS_DC) {
        return that_->PhpObjForId(this, id TSRMLS_CC);
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

  // Limited ObjectMapper for use during subclass initialization.
 protected:
  class JsOnlyMapper : public ObjectMapper {
  public:
      JsOnlyMapper(AsyncMessageWorker *worker) : worker_(worker) { }
      virtual objid_t IdForJsObj(const v8::Local<v8::Object> o) {
          return worker_->IdForJsObj(o);
      }
      virtual v8::Local<v8::Object> JsObjForId(objid_t id) {
          assert(false); return Nan::New<v8::Object>();
      }
      virtual objid_t IdForPhpObj(zval *o) {
          assert(false); return 0;
      }
      virtual zval * PhpObjForId(objid_t id TSRMLS_DC) {
          assert(false); return NULL;
      }
  private:
      AsyncMessageWorker *worker_;
  };

 private:
  void Execute() /*final override*/ {
    MessageChannel messageChannel(this);
    Execute(messageChannel);
  }

  void SendMessage_(MessageToJs *b) {
    uv_mutex_lock(&async_lock_);
    asyncdata_.push_back(b);
    uv_mutex_unlock(&async_lock_);

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
  uv_mutex_t async_lock_;
  std::list<MessageToJs *> asyncdata_;
  bool waitingForLock_;
  // Js Object mapping (along with GetFromPersistent/etc)
  // Read/writable only from Js thread
  Nan::Persistent<v8::NativeWeakMap> jsObjToId_;
  // PHP Object mapping
  // Read/writable only from PHP thread
  std::unordered_map<zend_object_handle,objid_t> phpObjToId_;
  std::vector<zval*> phpObjList_;
  // Ids are allocated from both threads, so mutex is required
  uv_mutex_t id_lock_;
  objid_t nextId_;
};

}
#endif

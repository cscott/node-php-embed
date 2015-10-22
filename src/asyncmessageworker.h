#ifndef ASYNCMESSAGEWORKER_H
#define ASYNCMESSAGEWORKER_H

#include <cassert>
#include <list>
#include <unordered_map>

#include <nan.h>

#include "messages.h"
#include "node_php_phpobject_class.h"
#include "node_php_jsobject_class.h"

#ifdef ZTS
#define TSRMLS_T void ***
#define TSRMLS_TC , TSRMLS_T
#else
#define TSRMLS_T
#define TSRMLS_TC
#endif

namespace node_php_embed {

class AsyncMessageWorker;

namespace amw {

// A queue of messages passed between threads.
class MessageQueue {
 public:
  MessageQueue(uv_async_t *async) : async_(async), data_() {
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
      // get one.
      uv_mutex_lock(&lock_);
      if (data_.empty()) {
        m = NULL;
        if (match) {
          // block for some data.
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
      if (match && match->IsProcessed()) { loop = false; }
    }
    return sawOne;
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

/* This is a proxy class, to avoid exposing the guts of AsyncMessageWorker
 * to the object proxies. */
class AsyncMapperChannel : public MapperChannel {
  friend class node_php_embed::AsyncMessageWorker;
 public:
  virtual ~AsyncMapperChannel() { }
   // JsObjectMapper interface
  virtual objid_t IdForJsObj(const v8::Local<v8::Object> o);
  virtual v8::Local<v8::Object> JsObjForId(objid_t id);
  // PhpObjectMapper interface
  virtual objid_t IdForPhpObj(zval *o);
  virtual zval *PhpObjForId(objid_t id TSRMLS_DC);
  // ObjectMapper interfaces
  virtual bool IsValid();
  // JsMessageChannel interface
  virtual void SendToJs(Message *m, bool isSync TSRMLS_DC) const;
  // PhpMessageChannel interface
  virtual void SendToPhp(Message *m, bool isSync) const;
 private:
  explicit AsyncMapperChannel(AsyncMessageWorker *that) : that_(that) { }
  NAN_DISALLOW_ASSIGN_COPY_MOVE(AsyncMapperChannel);
  AsyncMessageWorker* const that_;
};

} // amw namespace

/* This class is similar to Nan's AsyncProgressWorker, except that
 * we guarantee not to lose/discard messages sent from the worker,
 * and we've got special support for two-way message queues.
 */
/*abstract*/ class AsyncMessageWorker : public Nan::AsyncWorker {
  typedef std::pair<AsyncMessageWorker *,uv_loop_t*> worker_and_loop;
  friend class amw::AsyncMapperChannel;
 public:
  explicit AsyncMessageWorker(Nan::Callback *callback)
      : AsyncWorker(callback), channel_(this),
        js_queue_(new uv_async_t), php_queue_(new uv_async_t),
        js_is_sync_(0),
        php_obj_to_id_(), php_obj_list_(),
        // id #0 is reserved for "invalid object"
        next_id_(1) {
    // set up JS async loop (PHP side will be done in Execute)
    uv_async_init(uv_default_loop(), js_queue_.async(), JsAsyncMessage_);
    js_queue_.async()->data = new worker_and_loop(this, NULL);
    uv_mutex_init(&id_lock_);

    js_obj_to_id_.Reset(v8::NativeWeakMap::New(v8::Isolate::GetCurrent()));
  }

  // from the AsyncWorker superclass: once queued, Execute will run in the
  // PHP thread, then after it returns, WorkComplete() and Destroy() will
  // run in the JS thread.  WorkComplete handles the callbacks

 private:
  class JsCleanupSyncMsg : MessageToJs {
    friend class AsyncMessageWorker;
    JsCleanupSyncMsg(AsyncMessageWorker *that, ObjectMapper *m)
        : MessageToJs(m, NULL, true), that_(that) { }
    virtual void InJs(JsObjectMapper *m) {
      TRACE("> JsCleanupSyncMsg");
      // All previous PHP requests should have been serviced by now.
      objid_t last = that_->ClearAllJsIds();
      // Empty the JS side queue.
      that_->ProcessJs(NULL);
      // Ok, return to tell PHP the queues are empty.
      retval_.SetInt(last);
      TRACE("< JsCleanupSyncMsg");
    }
    objid_t GetLastId(ObjectMapper *m TSRMLS_DC) {
      ZVal last{ZEND_FILE_LINE_C};
      retval_.ToPhp(m, last TSRMLS_CC);
      assert(last.Type() == IS_LONG);
      return (objid_t) Z_LVAL_P(last.Ptr());
    }
   private:
    AsyncMessageWorker *that_;
  };
  void Execute() {
    TRACE("> AsyncMessageWorker");
    TSRMLS_FETCH();
    /* Start up an event loop for handling JS->PHP requests. */
    php_loop_ = new uv_loop_t;
    uv_loop_init(php_loop_);
    uv_async_init(php_loop_, php_queue_.async(), PhpAsyncMessage_);
    worker_and_loop *pair = new worker_and_loop(this, php_loop_);
    php_queue_.async()->data = pair;
    /* Now invoke the "real" Execute(), in the subclass. */
    Execute(&channel_ TSRMLS_CC);
    /* Start cleaning up. */
    objid_t last;
    {
      JsCleanupSyncMsg msg(this, &channel_);
      SendToJs(&msg, true TSRMLS_CC);
      last = msg.GetLastId(&channel_ TSRMLS_CC);
      // leave scope to dealloc msg before proceeding
    }
    ProcessPhp(NULL TSRMLS_CC); // shouldn't be necessary
    /* OK, queues are empty now, we can start tearing things down. */
    for (objid_t id = 1; id < last; id++) {
      ClearPhpId(id TSRMLS_CC); // probably not necessary, but can't hurt.
    }
    /* Hook for additional PHP-side shutdown. */
    AfterExecute(TSRMLS_C);
    /* Tear down loop and queue */
    pair->first = NULL; // can't touch asyncmessageworker after we return.
    uv_close(reinterpret_cast<uv_handle_t*>(php_queue_.async()),
             AsyncClose_); // completes async
    TRACE("> AsyncMessageWorker");
  }
  NAN_INLINE static void AsyncClose_(uv_handle_t* handle) {
    worker_and_loop *pair = static_cast<worker_and_loop*>(handle->data);
    assert(!pair->first);
    if (pair->second) {
      uv_loop_close(pair->second);
      delete pair->second;
    }
    delete reinterpret_cast<uv_async_t*>(handle);
  }
 public:
  virtual ~AsyncMessageWorker() {
    // PHP-side shutdown is complete by the time the destructor is called.
    // Tear down JS-side queue.  (Completion is async, but that's okay.)
    uv_async_t *async = js_queue_.async();
    worker_and_loop *pair = static_cast<worker_and_loop*>(async->data);
    pair->first = NULL; // can't touch asyncmessageworker after we return.
    uv_close(reinterpret_cast<uv_handle_t*>(async), AsyncClose_);
    // Mop up the pieces.
    js_obj_to_id_.Reset();
    uv_mutex_destroy(&id_lock_);
  }
  // This will execute on the PHP side.
  virtual void Execute(MapperChannel *mapperChannel TSRMLS_DC) = 0;
  // This does additional PHP side cleanup after Execute completes
  // and the queues have been emptied.
  virtual void AfterExecute(TSRMLS_D) { }

 protected:
  // Limited ObjectMapper for use during subclass initialization.
  class JsStartupMapper : public JsObjectMapper {
   public:
    JsStartupMapper(AsyncMessageWorker *worker) : worker_(worker) { }
    virtual objid_t IdForJsObj(const v8::Local<v8::Object> o) {
      return worker_->IdForJsObj(o);
    }
    virtual v8::Local<v8::Object> JsObjForId(objid_t id) {
      assert(false); return Nan::New<v8::Object>();
    }
   private:
    NAN_DISALLOW_ASSIGN_COPY_MOVE(JsStartupMapper);
    AsyncMessageWorker *worker_;
  };

 private:
  /* From both threads */

  inline objid_t NewId() {
    uv_mutex_lock(&id_lock_);
    // next_id_ is 0 if we're shutting down.
    objid_t id = (next_id_==0) ? 0 : (next_id_++);
    uv_mutex_unlock(&id_lock_);
    return id;
  }

  inline bool IsValid() {
    uv_mutex_lock(&id_lock_);
    bool valid = (next_id_!=0);
    uv_mutex_unlock(&id_lock_);
    return valid;
  }

  /*** JavaScript side ***/

  // Map Js object to an index (JS thread only)
  objid_t IdForJsObj(const v8::Local<v8::Object> o) {
    // Have we already mapped this?
    Nan::HandleScope scope;
    v8::Local<v8::NativeWeakMap> jsObjToId = Nan::New(js_obj_to_id_);
    if (jsObjToId->Has(o)) {
      return Nan::To<objid_t>(jsObjToId->Get(o)).FromJust();
    }

    objid_t id = NewId();
    jsObjToId->Set(o, Nan::New(id));
    SaveToPersistent(id, o);
    return id;
  }

  // Map index to JS object (or create it if necessary)
  v8::Local<v8::Object> JsObjForId(objid_t id) {
    Nan::EscapableHandleScope scope;
    Nan::MaybeLocal<v8::Object> maybeObj =
      Nan::To<v8::Object>(GetFromPersistent(id));
    if (!maybeObj.IsEmpty()) {
      return scope.Escape(maybeObj.ToLocalChecked());
    }
    // make a wrapper!
    v8::Local<v8::NativeWeakMap> jsObjToId = Nan::New(js_obj_to_id_);
    v8::Local<v8::Object> o = node_php_phpobject_create(&channel_, id);
    jsObjToId->Set(o, Nan::New(id));
    SaveToPersistent(id, o);
    return scope.Escape(o);
  }

  // Free JS references associated with an id (from JS thread)
  void ClearJsId(objid_t id) {
    Nan::HandleScope scope;
    Nan::MaybeLocal<v8::Object> o =
      Nan::To<v8::Object>(GetFromPersistent(id));
    if (o.IsEmpty()) { return; }
    // There might be other live references to this object; set its
    // id to 0 to neuter it.
    node_php_phpobject_maybe_neuter(o.ToLocalChecked());
    // Remove it from our maps (and release our persistent reference)
    v8::Local<v8::NativeWeakMap> jsObjToId = Nan::New(js_obj_to_id_);
    jsObjToId->Delete(o.ToLocalChecked());
    SaveToPersistent(id, Nan::Undefined());
  }

  objid_t ClearAllJsIds() {
    uv_mutex_lock(&id_lock_);
    objid_t last = next_id_;
    next_id_ = 0; // don't allocate any more ids.
    uv_mutex_unlock(&id_lock_);

    for (objid_t id = 1; id < last; id++) {
      ClearJsId(id);
    }
    return last;
  }

  void SendToPhp(Message *m, bool isSync) {
    assert(m);
    php_queue_.Push(m);
    if (isSync) {
      ProcessJs(m);
    }
  }

  bool ProcessJs(Message *match) {
    MapperChannel *channel = &channel_;
    return js_queue_.DoProcess(match, [channel](Message *mm) {
      mm->ExecuteJs(channel);
    });
  }

  NAN_INLINE static NAUV_WORK_CB(JsAsyncMessage_) {
    worker_and_loop *pair = static_cast<worker_and_loop*>(async->data);
    if (pair->first) {
      pair->first->ProcessJs(NULL);
    } else {
      NPE_ERROR("! JsAsyncMessage after shutdown"); // shouldn't happen
    }
  }

  /*** PHP side ***/

  // Map PHP object to an index (PHP thread only)
  objid_t IdForPhpObj(zval *z) {
    assert(Z_TYPE_P(z) == IS_OBJECT);
    zend_object_handle handle = Z_OBJ_HANDLE_P(z);
    if (php_obj_to_id_.count(handle)) {
      return php_obj_to_id_.at(handle);
    }

    objid_t id = NewId();
    if (id >= php_obj_list_.size()) { php_obj_list_.resize(id + 1); }
    // XXX should we clone/separate z?
    Z_ADDREF_P(z);
    php_obj_list_[id] = z;
    php_obj_to_id_[handle] = id;
    return id;
  }

  // Returned value is owned by objectmapper, caller should not release it.
  zval * PhpObjForId(objid_t id TSRMLS_DC) {
    if (id >= php_obj_list_.size()) { php_obj_list_.resize(id + 1); }
    ZVal z(php_obj_list_[id] ZEND_FILE_LINE_CC);
    if (z.IsNull()) {
      node_php_jsobject_create(z.Ptr(), &channel_, id TSRMLS_CC);
      php_obj_list_[id] = z.Ptr();
      php_obj_to_id_[Z_OBJ_HANDLE_P(z.Ptr())] = id;
      // one excess reference: owned by objectmapper
      return z.Escape();
    }
    // don't increment reference
    return z.Ptr();
  }

  // Free PHP references associated with an id (from PHP thread)
  void ClearPhpId(objid_t id TSRMLS_DC) {
    zval *z = (id < php_obj_list_.size()) ? php_obj_list_[id] : NULL;
    if (z) {
      node_php_jsobject_maybe_neuter(z TSRMLS_CC);
      php_obj_list_[id] = NULL;
      php_obj_to_id_.erase(Z_OBJ_HANDLE_P(z));
      zval_ptr_dtor(&z);
    }
  }

  void SendToJs(Message *m, bool isSync TSRMLS_DC) {
    assert(m);
    js_queue_.Push(m);
    if (isSync) {
      ProcessPhp(m TSRMLS_CC);
    }
  }

  void ProcessPhp(Message *match TSRMLS_DC) {
    MapperChannel *channel = &channel_;
    php_queue_.DoProcess(match, [channel TSRMLS_CC](Message *mm) {
      mm->ExecutePhp(channel TSRMLS_CC);
    });
  }

  NAN_INLINE static NAUV_WORK_CB(PhpAsyncMessage_) {
    worker_and_loop *pair = static_cast<worker_and_loop*>(async->data);
    if (pair->first) {
      TSRMLS_FETCH();
      pair->first->ProcessPhp(NULL TSRMLS_CC);
    } else {
      NPE_ERROR("! PhpAsyncMessage after shutdown"); // shouldn't happen
    }
  }

  /** Member fields **/

  // Inner interface to export a clean API.
  amw::AsyncMapperChannel channel_;

  // Queue for messages between PHP to JS
  amw::MessageQueue js_queue_;
  amw::MessageQueue php_queue_;
  // PHP event loop
  uv_loop_t *php_loop_;
  // deadlock prevention
  int js_is_sync_;

  // Js Object mapping (along with GetFromPersistent/etc)
  // Read/writable only from Js thread
  Nan::Persistent<v8::NativeWeakMap> js_obj_to_id_;

  // PHP Object mapping
  // Read/writable only from PHP thread
  std::unordered_map<zend_object_handle,objid_t> php_obj_to_id_;
  std::vector<zval*> php_obj_list_;

  // Ids are allocated from both threads, so mutex is required
  uv_mutex_t id_lock_;
  objid_t next_id_;
};

// Implement AsyncMapperChannel proxies (after AsyncMessageWorker
// is defined).

// JsObjectMapper interface
objid_t amw::AsyncMapperChannel::IdForJsObj(const v8::Local<v8::Object> o) {
  return that_->IdForJsObj(o);
}
v8::Local<v8::Object> amw::AsyncMapperChannel::JsObjForId(objid_t id) {
  return that_->JsObjForId(id);
}
// PhpObjectMapper interface
objid_t amw::AsyncMapperChannel::IdForPhpObj(zval *o) {
  return that_->IdForPhpObj(o);
}
zval *amw::AsyncMapperChannel::PhpObjForId(objid_t id TSRMLS_DC) {
  return that_->PhpObjForId(id TSRMLS_CC);
}
// ObjectMapper interfaces
bool amw::AsyncMapperChannel::IsValid() {
  return that_->IsValid();
}
// JsMessageChannel interface
void amw::AsyncMapperChannel::SendToJs(Message *m, bool isSync TSRMLS_DC) const {
  that_->SendToJs(m, isSync TSRMLS_CC);
}
// PhpMessageChannel interface
void amw::AsyncMapperChannel::SendToPhp(Message *m, bool isSync) const {
  that_->SendToPhp(m, isSync);
}

}
#endif

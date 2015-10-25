// AsyncMessageWorker handles message delivery between the JS and PHP threads.

// Copyright (c) 2015 C. Scott Ananian <cscott@cscott.net>
#ifndef NODE_PHP_EMBED_ASYNCMESSAGEWORKER_H_
#define NODE_PHP_EMBED_ASYNCMESSAGEWORKER_H_

#include <cassert>
#include <unordered_map>
#include <vector>

#include "nan.h"

extern "C" {
#include "main/php.h"
}

#include "src/messages.h"
#include "src/messagequeue.h"
#include "src/node_php_phpobject_class.h"
#include "src/node_php_jsobject_class.h"

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

/* This is the interface exposed to the object proxy classes. */
class AsyncMapperChannel : public MapperChannel {
  friend class node_php_embed::AsyncMessageWorker;
 public:
  virtual ~AsyncMapperChannel() {
    // zvals should have been freed beforehand from php_obj_list_ because
    // PHP context is shut down so we can't do that now.
    js_obj_to_id_.Reset();
    uv_mutex_destroy(&id_lock_);
  }
  // JsObjectMapper interface
  objid_t IdForJsObj(const v8::Local<v8::Object> o) override;
  v8::Local<v8::Object> JsObjForId(objid_t id) override;
  // PhpObjectMapper interface
  objid_t IdForPhpObj(zval *o) override;
  zval *PhpObjForId(objid_t id TSRMLS_DC) override;
  // ObjectMapper interfaces
  bool IsValid() override;
  // JsMessageChannel interface
  void SendToJs(Message *m, MessageFlags flags TSRMLS_DC) const override;
  // PhpMessageChannel interface
  void SendToPhp(Message *m, MessageFlags flags) const override;

 private:
  // Callable from both threads:
  objid_t NewId();
  // Callable from JS thread:
  void ClearJsId(objid_t id);
  objid_t ClearAllJsIds();
  // Callable from PHP thread:
  void ClearPhpId(objid_t id TSRMLS_DC);
  // Constructor, invoked from JS thread:
  explicit AsyncMapperChannel(AsyncMessageWorker *worker)
      : worker_(worker), js_obj_to_id_(), php_obj_to_id_(), php_obj_list_(),
        // Id #0 is reserved for "invalid object".
        next_id_(1) {
    uv_mutex_init(&id_lock_);
    js_obj_to_id_.Reset(v8::NativeWeakMap::New(v8::Isolate::GetCurrent()));
  }
  NAN_DISALLOW_ASSIGN_COPY_MOVE(AsyncMapperChannel);
  AsyncMessageWorker* worker_;

  // Js Object mapping (along with worker's GetFromPersistent/etc)
  // Read/writable only from Js thread.
  Nan::Persistent<v8::NativeWeakMap> js_obj_to_id_;

  // PHP Object mapping
  // Read/writable only from PHP thread.
  std::unordered_map<zend_object_handle, objid_t> php_obj_to_id_;
  std::vector<zval*> php_obj_list_;

  // Ids are allocated from both threads, so mutex is required.
  uv_mutex_t id_lock_;
  objid_t next_id_;
};

}  // namespace amw

/* This class is similar to Nan's AsyncProgressWorker, except that
 * we guarantee not to lose/discard messages sent from the worker,
 * and we've got special support for two-way message queues.
 */
/*abstract*/ class AsyncMessageWorker : public Nan::AsyncWorker {
  friend class amw::AsyncMapperChannel;
  friend class JsStartupMapper;

 public:
  explicit AsyncMessageWorker(Nan::Callback *callback)
      : AsyncWorker(callback), channel_(this),
        // Create a no-op function to allow us to kick off node's next tick
        kick_next_tick_(Nan::New<v8::Function>(NoOpFunction_, Nan::Null())),
        // Queues for messages between PHP and JS.
        js_queue_(new uv_async_t),
        php_queue_(new uv_async_t),
        js_is_sync_(0) {
    // Set up JS async loop (PHP side will be done in Execute).
    uv_async_init(uv_default_loop(), js_queue_.async(), JsAsyncMessage_);
    js_queue_.async()->data = this;
  }

  // From the AsyncWorker superclass: once queued, Execute will run in the
  // PHP thread, then after it returns, WorkComplete() and Destroy() will
  // run in the JS thread.  WorkComplete handles the final callbacks.

  virtual ~AsyncMessageWorker() {
    TRACE(">");
    // PHP-side shutdown is complete by the time the destructor is called.
    // Tear down JS-side queue.  (Completion is async, but that's okay.)
    uv_async_t *async = js_queue_.async();
    async->data = NULL;  // can't touch asyncmessageworker after we return.
    uv_close(reinterpret_cast<uv_handle_t*>(async), AsyncClose_);
    TRACE("<");
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
    explicit JsStartupMapper(AsyncMessageWorker *worker)
        : channel_(&(worker->channel_)) { }
    objid_t IdForJsObj(const v8::Local<v8::Object> o) override {
      return channel_->IdForJsObj(o);
    }
    v8::Local<v8::Object> JsObjForId(objid_t id) override {
      assert(false); return Nan::New<v8::Object>();
    }
   private:
    NAN_DISALLOW_ASSIGN_COPY_MOVE(JsStartupMapper);
    amw::AsyncMapperChannel *channel_;
  };

  virtual void HandleOKCallback(JsObjectMapper *m) {
    callback->Call(0, NULL);
  }

 private:
  // Allow subclass to have access to a JsObjectMapper in the OKCallback.
  void HandleOKCallback() final {
    HandleOKCallback(&channel_);
  }

  class JsCleanupSyncMsg : MessageToJs {
    friend class AsyncMessageWorker;
    explicit JsCleanupSyncMsg(AsyncMessageWorker *that)
        : MessageToJs(&(that->channel_), NULL, true), that_(that) { }
    void InJs(JsObjectMapper *m) override {
      TRACE("> JsCleanupSyncMsg");
      // All previous PHP requests should have been serviced by now.
      objid_t last = that_->channel_.ClearAllJsIds();
      // Empty the JS side queue.
      that_->ProcessJs(NULL, false /* we're inside a ProcessJs already */);
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

   protected:
    // Shutdown the JS queue after the response to this message.
    bool IsShutdown() override { return true; }

   private:
    AsyncMessageWorker *that_;
  };

  void Execute() final {
    TRACE("> AsyncMessageWorker");
    TSRMLS_FETCH();
    /* Start up an event loop for handling JS->PHP requests. */
    php_loop_ = new uv_loop_t;
    uv_loop_init(php_loop_);
    uv_async_t *a = php_queue_.async();
    uv_async_init(php_loop_, a, PhpAsyncMessage_);
    a->data = this;
    // Unref the async handle so it doesn't prevent the loop from finishing.
    uv_unref(reinterpret_cast<uv_handle_t*>(php_queue_.async()));
    /* Now invoke the "real" Execute(), in the subclass. */
    Execute(&channel_ TSRMLS_CC);
    // Now run any pending async tasks, until there are no more.
    // This turns PHP into a NodeJS-style execution model!
    uv_run(php_loop_, UV_RUN_DEFAULT);
    /* Start cleaning up. */
    objid_t last;
    {
      JsCleanupSyncMsg msg(this);
      SendToJs(&msg, MessageFlags::SYNC TSRMLS_CC);
      last = msg.GetLastId(&channel_ TSRMLS_CC);
      // Exit this scope to dealloc msg before proceeding.
    }
    ProcessPhp(NULL TSRMLS_CC);  // A precaution; shouldn't be necessary.
    a->data = NULL;
    js_queue_.Shutdown();
    /* OK, queues are empty now, we can start tearing things down. */
    for (objid_t id = 1; id < last; id++) {
      // zvals need to be cleared on the PHP side.
      // We're about to deallocate the entire pool used by the request,
      // but this helps catch leaks.
      channel_.ClearPhpId(id TSRMLS_CC);
    }
    /* Hook for additional PHP-side shutdown. */
    AfterExecute(TSRMLS_C);
    /* Tear down loop and queue */
    // This close operation completes in the php_loop_
    uv_close(reinterpret_cast<uv_handle_t*>(a), AsyncClose_);
    uv_run(php_loop_, UV_RUN_DEFAULT);  // Let the close complete.
    uv_loop_close(php_loop_);
    delete php_loop_;
    TRACE("< AsyncMessageWorker");
  }

  NAN_INLINE static void AsyncClose_(uv_handle_t* handle) {
    TRACE(">");
#ifndef NDEBUG
    AsyncMessageWorker *worker = static_cast<AsyncMessageWorker*>(handle->data);
    assert(!worker);
#endif
    delete reinterpret_cast<uv_async_t*>(handle);
    TRACE("<");
  }


  /*** Methods callable only from the JavaScript side ***/

  void SendToPhp(Message *m, MessageFlags flags) {
    bool isSync = has_flags(flags, MessageFlags::SYNC);
    bool isResponse = has_flags(flags, MessageFlags::RESPONSE);
    bool isShutdown = has_flags(flags, MessageFlags::SHUTDOWN);
    assert(m); assert(!(isSync && isResponse));
    php_queue_.Push(m);
    if ((!isResponse) && (isSync || js_is_sync_)) {
      TRACE("! JS IS SYNC");
      js_is_sync_++;
      ProcessJs(m, false /* not top level, don't kick the tick */);
      js_is_sync_--;
    }
    if (isShutdown) {
      php_queue_.Shutdown();
    }
  }

  void ProcessJs(Message *match, bool kickNextTick) {
    MapperChannel *channel = &channel_;
    // Start a handle scope.
    Nan::HandleScope handle_scope;
    // Enter appropriate context
    v8::Context::Scope scope(kick_next_tick_.GetFunction()->CreationContext());
    js_queue_.DoProcess(match, [channel](Message *mm) {
      // Each message will get its own handle scope.
      Nan::HandleScope scope;
      mm->ExecuteJs(channel);
    });
    // Kick the tick.  See:
    // https://github.com/nodejs/nan/issues/284#issuecomment-150887627
    if (kickNextTick) {
      kick_next_tick_.Call(0, NULL);
    }
  }

  NAN_INLINE static NAUV_WORK_CB(JsAsyncMessage_) {
    AsyncMessageWorker *worker = static_cast<AsyncMessageWorker*>(async->data);
    if (worker) {
      worker->ProcessJs(NULL, true /* from uv loop, kick next tick */);
    } else {
      NPE_ERROR("! JsAsyncMessage after shutdown");  // Shouldn't happen.
    }
  }


  /*** Methods callable only from the PHP side ***/

  void SendToJs(Message *m, MessageFlags flags TSRMLS_DC) {
    bool isSync = has_flags(flags, MessageFlags::SYNC);
    bool isResponse = has_flags(flags, MessageFlags::RESPONSE);
    bool isShutdown = has_flags(flags, MessageFlags::SHUTDOWN);
    assert(m); assert(!(isSync && isResponse));
    js_queue_.Push(m);
    if (isSync) {
      ProcessPhp(m TSRMLS_CC);
    }
    if (isShutdown) {
      js_queue_.Shutdown();
    }
  }

  void ProcessPhp(Message *match TSRMLS_DC) {
    MapperChannel *channel = &channel_;
    php_queue_.DoProcess(match, [channel TSRMLS_CC](Message *mm) {
      mm->ExecutePhp(channel TSRMLS_CC);
    });
  }

  NAN_INLINE static NAUV_WORK_CB(PhpAsyncMessage_) {
    AsyncMessageWorker *worker = static_cast<AsyncMessageWorker*>(async->data);
    if (worker) {
      TSRMLS_FETCH();
      worker->ProcessPhp(NULL TSRMLS_CC);
    } else {
      NPE_ERROR("! PhpAsyncMessage after shutdown");  // Shouldn't happen.
    }
  }

  static void NoOpFunction_(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    // do nothing here; it's only purpose is to kick off the next node tick.
  }

  /*** Member fields ***/

  // Inner interface to export a clean API.
  amw::AsyncMapperChannel channel_;
  // A no-op function to allow us to kick off node's next tick
  Nan::Callback kick_next_tick_;

  // Queue for messages between PHP to JS.
  MessageQueue js_queue_;
  MessageQueue php_queue_;
  // PHP event loop.
  uv_loop_t *php_loop_;
  // Deadlock prevention.
  int js_is_sync_;
};

// AsyncMapperChannel implementation.

// JsObjectMapper interface -----------------------
// Callable only from the JavaScript side.

// Map Js object to an index.
objid_t amw::AsyncMapperChannel::IdForJsObj(const v8::Local<v8::Object> o) {
  // Have we already mapped this?
  Nan::HandleScope scope;
  v8::Local<v8::NativeWeakMap> jsObjToId = Nan::New(js_obj_to_id_);
  if (jsObjToId->Has(o)) {
    return Nan::To<objid_t>(jsObjToId->Get(o)).FromJust();
  }

  // XXX If o is a Promise, then call PrFunPromise.resolve(o),
  // and set both of them in the jsObjToId map. This would ensure
  // that Promise#nodify is available from PHP.

  objid_t id = NewId();
  jsObjToId->Set(o, Nan::New(id));
  worker_->SaveToPersistent(id, o);
  return id;
}

// Map index to JS object (or create it if necessary).
v8::Local<v8::Object> amw::AsyncMapperChannel::JsObjForId(objid_t id) {
  Nan::EscapableHandleScope scope;
  Nan::MaybeLocal<v8::Object> maybeObj =
    Nan::To<v8::Object>(worker_->GetFromPersistent(id));
  if (!maybeObj.IsEmpty()) {
    return scope.Escape(maybeObj.ToLocalChecked());
  }
  // Make a wrapper!
  v8::Local<v8::NativeWeakMap> jsObjToId = Nan::New(js_obj_to_id_);
  v8::Local<v8::Object> o = node_php_phpobject_create(this, id);
  jsObjToId->Set(o, Nan::New(id));
  worker_->SaveToPersistent(id, o);
  return scope.Escape(o);
}

  // Free JS references associated with an id.
void amw::AsyncMapperChannel::ClearJsId(objid_t id) {
  Nan::HandleScope scope;
  Nan::MaybeLocal<v8::Object> o =
    Nan::To<v8::Object>(worker_->GetFromPersistent(id));
  if (o.IsEmpty()) { return; }
  // There might be other live references to this object; set its
  // id to 0 to neuter it.
  node_php_phpobject_maybe_neuter(o.ToLocalChecked());
  // Remove it from our maps (and release our persistent reference).
  v8::Local<v8::NativeWeakMap> jsObjToId = Nan::New(js_obj_to_id_);
  jsObjToId->Delete(o.ToLocalChecked());
  worker_->SaveToPersistent(id, Nan::Undefined());
}

objid_t amw::AsyncMapperChannel::ClearAllJsIds() {
  uv_mutex_lock(&id_lock_);
  objid_t last = next_id_;
  next_id_ = 0;  // Don't allocate any more ids.
  uv_mutex_unlock(&id_lock_);

  for (objid_t id = 1; id < last; id++) {
    ClearJsId(id);
  }
  return last;
}

// PhpObjectMapper interface -----------------------
// Callable only from the PHP side.

// Map PHP object to an index.
objid_t amw::AsyncMapperChannel::IdForPhpObj(zval *z) {
  assert(Z_TYPE_P(z) == IS_OBJECT);
  zend_object_handle handle = Z_OBJ_HANDLE_P(z);
  if (php_obj_to_id_.count(handle)) {
    return php_obj_to_id_.at(handle);
  }

  objid_t id = NewId();
  if (id >= php_obj_list_.size()) { php_obj_list_.resize(id + 1); }
  // XXX Should we clone/separate z?
  Z_ADDREF_P(z);
  php_obj_list_[id] = z;
  php_obj_to_id_[handle] = id;
  return id;
}

// Returned value is owned by the object mapper, caller should not release it.
zval *amw::AsyncMapperChannel::PhpObjForId(objid_t id TSRMLS_DC) {
  if (id >= php_obj_list_.size()) { php_obj_list_.resize(id + 1); }
  ZVal z(php_obj_list_[id] ZEND_FILE_LINE_CC);
  if (z.IsNull()) {
    node_php_jsobject_create(z.Ptr(), this, id TSRMLS_CC);
    php_obj_list_[id] = z.Ptr();
    php_obj_to_id_[Z_OBJ_HANDLE_P(z.Ptr())] = id;
    // One excess reference, owned by objectmapper.
    return z.Escape();
  }
  // Don't increment reference.
  return z.Ptr();
}

// Free PHP references associated with an id.
void amw::AsyncMapperChannel::ClearPhpId(objid_t id TSRMLS_DC) {
  zval *z = (id < php_obj_list_.size()) ? php_obj_list_[id] : NULL;
  if (z) {
    node_php_jsobject_maybe_neuter(z TSRMLS_CC);
    php_obj_list_[id] = NULL;
    php_obj_to_id_.erase(Z_OBJ_HANDLE_P(z));
    zval_ptr_dtor(&z);
  }
}

// ObjectMapper interface -----------------------
// Callable from both threads.

objid_t amw::AsyncMapperChannel::NewId() {
  uv_mutex_lock(&id_lock_);
  // next_id_ is 0 if we're shutting down.
  objid_t id = (next_id_ == 0) ? 0 : (next_id_++);
  uv_mutex_unlock(&id_lock_);
  return id;
}

bool amw::AsyncMapperChannel::IsValid() {
  uv_mutex_lock(&id_lock_);
  bool valid = (next_id_ != 0);
  uv_mutex_unlock(&id_lock_);
  return valid;
}

// JsMessageChannel interface -----------------------
// Callable only from the PHP side.
void amw::AsyncMapperChannel::SendToJs(Message *m, MessageFlags flags
                                       TSRMLS_DC) const {
  worker_->SendToJs(m, flags TSRMLS_CC);
}
// PhpMessageChannel interface -----------------------
// Callable only from the JS side.
void amw::AsyncMapperChannel::SendToPhp(Message *m, MessageFlags flags) const {
  worker_->SendToPhp(m, flags);
}

}  // namespace node_php_embed

#endif  // NODE_PHP_EMBED_ASYNCMESSAGEWORKER_H_

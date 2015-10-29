// AsyncMessageWorker handles message delivery between the JS and PHP threads.

// Copyright (c) 2015 C. Scott Ananian <cscott@cscott.net>
#include "src/asyncmessageworker.h"

#include <cassert>

#include "nan.h"

extern "C" {
#include "main/php.h"
}

#include "src/asyncmapperchannel.h"
#include "src/messages.h"
#include "src/messagequeue.h"
#include "src/node_php_phpobject_class.h"
#include "src/node_php_jsobject_class.h"

namespace node_php_embed {

/* The AsyncMessageWorker class is similar to Nan's
 * AsyncProgressWorker, except that we guarantee not to lose/discard
 * messages sent from the worker, and we've got special support for
 * two-way message queues.
 */
AsyncMessageWorker::AsyncMessageWorker(Nan::Callback *callback)
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

AsyncMessageWorker::~AsyncMessageWorker() {
  TRACE(">");
  // PHP-side shutdown is complete by the time the destructor is called.
  // Tear down JS-side queue.  (Completion is async, but that's okay.)
  uv_async_t *async = js_queue_.async();
  async->data = nullptr;  // can't touch asyncmessageworker after we return.
  uv_close(reinterpret_cast<uv_handle_t*>(async), AsyncClose_);
  TRACE("<");
}

class JsCleanupSyncMsg : MessageToJs {
  friend class AsyncMessageWorker;
  explicit JsCleanupSyncMsg(AsyncMessageWorker *that)
      : MessageToJs(&(that->channel_), nullptr, true), that_(that) { }
  void InJs(JsObjectMapper *m) override {
    TRACE("> JsCleanupSyncMsg");
    // All previous PHP requests should have been serviced by now.
    objid_t last = that_->channel_.ClearAllJsIds();
    // Empty the JS side queue.
    that_->ProcessJs(nullptr, false /* we're inside a ProcessJs already */);
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

void AsyncMessageWorker::Execute() {
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
  /* Flush the buffers, send the headers. */
  AfterAsyncLoop(TSRMLS_C);
  /* Start cleaning up. */
  objid_t last;
  {
    JsCleanupSyncMsg msg(this);
    SendToJs(&msg, MessageFlags::SYNC TSRMLS_CC);
    last = msg.GetLastId(&channel_ TSRMLS_CC);
    // Exit this scope to dealloc msg before proceeding.
  }
  ProcessPhp(nullptr TSRMLS_CC);  // A precaution; shouldn't be necessary.
  a->data = nullptr;
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

void AsyncMessageWorker::AsyncClose_(uv_handle_t* handle) {
  TRACE(">");
#ifndef NDEBUG
  AsyncMessageWorker *worker = static_cast<AsyncMessageWorker*>(handle->data);
  assert(!worker);
#endif
  delete reinterpret_cast<uv_async_t*>(handle);
  TRACE("<");
}

/*** Methods callable only from the JavaScript side ***/

void AsyncMessageWorker::SendToPhp(Message *m, MessageFlags flags) {
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

void AsyncMessageWorker::ProcessJs(Message *match, bool kickNextTick) {
  MapperChannel *channel = &channel_;
  bool js_is_sync = js_is_sync_;
  // Start a handle scope.
  Nan::HandleScope handle_scope;
  // Enter appropriate context
  v8::Context::Scope scope(kick_next_tick_.GetFunction()->CreationContext());
  js_queue_.DoProcess(match, [channel, js_is_sync](Message *mm) {
    // Each message will get its own handle scope.
    Nan::HandleScope scope;
    mm->ExecuteJs(channel, js_is_sync);
  });
  // Kick the tick.  See:
  // https://github.com/nodejs/nan/issues/284#issuecomment-150887627
  if (kickNextTick) {
    kick_next_tick_.Call(0, nullptr);
  }
}

NAUV_WORK_CB(AsyncMessageWorker::JsAsyncMessage_) {
  AsyncMessageWorker *worker = static_cast<AsyncMessageWorker*>(async->data);
  if (worker) {
    worker->ProcessJs(nullptr, true /* from uv loop, kick next tick */);
  } else {
    NPE_ERROR("! JsAsyncMessage after shutdown");  // Shouldn't happen.
  }
}


/*** Methods callable only from the PHP side ***/

void AsyncMessageWorker::SendToJs(Message *m, MessageFlags flags TSRMLS_DC) {
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

void AsyncMessageWorker::ProcessPhp(Message *match TSRMLS_DC) {
  MapperChannel *channel = &channel_;
  php_queue_.DoProcess(match, [channel TSRMLS_CC](Message *mm) {
    mm->ExecutePhp(channel TSRMLS_CC);
  });
}

NAUV_WORK_CB(AsyncMessageWorker::PhpAsyncMessage_) {
  AsyncMessageWorker *worker = static_cast<AsyncMessageWorker*>(async->data);
  if (worker) {
    TSRMLS_FETCH();
    worker->ProcessPhp(nullptr TSRMLS_CC);
  } else {
    NPE_ERROR("! PhpAsyncMessage after shutdown");  // Shouldn't happen.
  }
}

}  // namespace node_php_embed

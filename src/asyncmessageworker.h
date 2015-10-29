// AsyncMessageWorker handles message delivery between the JS and PHP threads.

// Copyright (c) 2015 C. Scott Ananian <cscott@cscott.net>
#ifndef NODE_PHP_EMBED_ASYNCMESSAGEWORKER_H_
#define NODE_PHP_EMBED_ASYNCMESSAGEWORKER_H_

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

/* This class is similar to Nan's AsyncProgressWorker, except that
 * we guarantee not to lose/discard messages sent from the worker,
 * and we've got special support for two-way message queues.
 */
/*abstract*/ class AsyncMessageWorker : public Nan::AsyncWorker {
  friend class amw::AsyncMapperChannel;
  friend class JsStartupMapper;
  friend class JsCleanupSyncMsg;

 public:
  explicit AsyncMessageWorker(Nan::Callback *callback);

  // From the AsyncWorker superclass: once queued, Execute will run in the
  // PHP thread, then after it returns, WorkComplete() and Destroy() will
  // run in the JS thread.  WorkComplete handles the final callbacks.

  virtual ~AsyncMessageWorker();

  // This will execute on the PHP side.
  virtual void Execute(MapperChannel *mapperChannel TSRMLS_DC) = 0;

  // This provides one last chance to flush buffers and send headers
  // after the async loop has finished and before queues are shut down.
  virtual void AfterAsyncLoop(TSRMLS_D) { }

  // This does additional PHP side cleanup after Execute completes
  // and the queues have been emptied.
  virtual void AfterExecute(TSRMLS_D) { }

  // We have SaveTo and GetFrom; we need DeleteFrom as well.
  NAN_INLINE void DeleteFromPersistent(uint32_t index) {
    Nan::HandleScope scope;
    Nan::New(persistentHandle)->Delete(index);
  }

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
    callback->Call(0, nullptr);
  }

 private:
  // Allow subclass to have access to a JsObjectMapper in the OKCallback.
  void HandleOKCallback() final {
    HandleOKCallback(&channel_);
  }

  void Execute() final;
  static void AsyncClose_(uv_handle_t* handle);

  /*** Methods callable only from the JavaScript side ***/

  void SendToPhp(Message *m, MessageFlags flags);
  void ProcessJs(Message *match, bool kickNextTick);
  static NAUV_WORK_CB(JsAsyncMessage_);

  /*** Methods callable only from the PHP side ***/

  void SendToJs(Message *m, MessageFlags flags TSRMLS_DC);
  void ProcessPhp(Message *match TSRMLS_DC);
  static NAUV_WORK_CB(PhpAsyncMessage_);

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

}  // namespace node_php_embed

#endif  // NODE_PHP_EMBED_ASYNCMESSAGEWORKER_H_

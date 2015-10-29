// Definitions of message types and basic message-passing infrastructure.

// Copyright (c) 2015 C. Scott Ananian <cscott@cscott.net>
#ifndef NODE_PHP_EMBED_MESSAGES_H_
#define NODE_PHP_EMBED_MESSAGES_H_

#include <iostream>

#include "nan.h"

extern "C" {
#include "Zend/zend_exceptions.h"  // for zend_clear_exception
}

#include "src/macros.h"
#include "src/values.h"

namespace node_php_embed {

class Message;

enum class MessageFlags { ASYNC = 0, SYNC = 1, RESPONSE = 2, SHUTDOWN = 4 };

// Very simple bitmask enum operations.
constexpr int operator*(MessageFlags f) { return static_cast<int>(f); }
constexpr MessageFlags operator|(MessageFlags f1, MessageFlags f2) {
  return MessageFlags(*f1 | *f2);
}
static inline bool has_flags(MessageFlags f1, MessageFlags f2) {
  return ((*f1) & (*f2)) == (*f2);
}

// Interfaces for sending messages to different threads.
class JsMessageChannel {
 public:
  virtual ~JsMessageChannel() { }
  // If is_sync is true, will not return until response has been received.
  virtual void SendToJs(Message *m, MessageFlags flags TSRMLS_DC) const = 0;
};
class PhpMessageChannel {
 public:
  virtual ~PhpMessageChannel() { }
  // If is_sync is true, will not return until response has been received.
  virtual void SendToPhp(Message *m, MessageFlags flags) const = 0;
};

// Helpful mechanism for passing all these interfaces around as a
// single pointer.
class MapperChannel
    : public ObjectMapper, public JsMessageChannel, public PhpMessageChannel {
 public:
  virtual ~MapperChannel() { }
};

// A message sent between threads.
// All messages are constructed on thread A, have a "request" part
// executed on thread B, and then (optionally) a "response" part
// executed on thread A again (perhaps sync, perhaps async).
class Message {
 public:
  explicit Message(ObjectMapper *mapper)
      : mapper_(mapper), retval_(), exception_(), processed_(false) { }
  virtual ~Message() { }
  inline bool HasException() { return !exception_.IsEmpty(); }
  inline bool IsProcessed() { return processed_; }
  inline const Value &retval() { return retval_; }
  inline const Value &exception() { return exception_; }
  // Override this if you want to explicitly allow the return value to
  // be 'empty' -- this is used in JS getters to indicate lookup should
  // continue up the prototype chain, for instance.
  virtual bool IsEmptyRetvalOk() { return false; }

  // We don't know which of these is the "request" or "response" part
  // yet, but we'll name them by execution context, and we'll
  // provide them with access to the message channel so they can
  // send responses to the other side (if they happen to be the
  // request context).
  virtual void ExecutePhp(JsMessageChannel *channel TSRMLS_DC) = 0;
  // Pass true to no_async if returning a value via a Node callback will
  // deadlock the process.
  virtual void ExecuteJs(PhpMessageChannel *channel, bool no_async) = 0;

 protected:
  ObjectMapper *mapper_;
  Value retval_, exception_;
  bool processed_;
};

// This is a message constructed in JS, where the request is handled
// in PHP and the response is handled in JavaScript.
class MessageToPhp : public Message {
 public:
  // Constructed in JS thread.  The callback may be nullptr for
  // fire-and-forget methods.  If provided, it will be invoked
  // nodejs-style, with exception as first arg and retval as second.
  // The callback will be owned by the message, and deleted after use.
  // For sync calls, the callback should be null and is_sync should be true.
  MessageToPhp(ObjectMapper *m, Nan::Callback *callback, bool is_sync)
      : Message(m), callback_(callback), is_sync_(is_sync) {
    assert(is_sync ? !callback : true);
  }
  virtual ~MessageToPhp() {
    if (callback_) { delete callback_; }
  }
  inline bool IsSync() { return is_sync_; }
  // This is the "request" portion of the message, executed on the PHP
  // side and sending its result back to JS.
  void ExecutePhp(JsMessageChannel *channel TSRMLS_DC) override {
    if (mapper_->IsValid()) {
      InPhp(mapper_ TSRMLS_CC);
      if (EG(exception)) {
        exception_.Set(mapper_, EG(exception) TSRMLS_CC);
        zend_clear_exception(TSRMLS_C);
      }
    }
    if (retval_.IsEmpty() && exception_.IsEmpty()) {
      // If no result, throw an exception.
      const char *msg = (!mapper_->IsValid()) ? "shutdown" :
        (!IsEmptyRetvalOk()) ? "no return value" : nullptr;
      if (msg) {
        exception_.SetString(msg, strlen(msg));
      }
    }
    // Now send response back to JS side.
    // (Even for fire-and-forget messages, we want to execute the
    // destructor in the same thread as the constructor, so that
    // means we need to do a context switch back to JS.)
    channel->SendToJs(this, MessageFlags::ASYNC | MessageFlags::RESPONSE
                      TSRMLS_CC);
  }
  // This is the "response" portion of the message, executed on the JS
  // side to dispatch results and/or cleanup.
  void ExecuteJs(PhpMessageChannel *channel, bool no_async) override {
    processed_ = true;
    if (is_sync_) {
      return;  // Caller will handle return value & exceptions.
    }
    // Ensure we always invoke ToJs on the results, even for fire-and-forget.
    // This ensures that any PHP objects sent from PHP to JS get instantiated
    // on the JS side, so that their JS gc destructors can eventually free
    // the object from the PHP side.
    Nan::HandleScope scope;
    v8::Local<v8::Value> r, e;
    if (HasException()) {
      e = exception_.ToJs(mapper_);
    } else {
      r = retval_.ToJs(mapper_);
    }
    if (callback_) {
      Nan::TryCatch tryCatch;
      if (HasException()) {
        v8::Local<v8::Value> error = Nan::Error("PHP Exception");
        Nan::MaybeLocal<v8::Object> errObj = Nan::To<v8::Object>(error);
        if (!errObj.IsEmpty()) {
          Nan::Set(errObj.ToLocalChecked(), NEW_STR("nativeError"), e);
        }
        v8::Local<v8::Value> argv[] = { error };
        callback_->Call(1, argv);
      } else {
        v8::Local<v8::Value> argv[] = { Nan::Null(), r };
        callback_->Call(2, argv);
      }
      if (tryCatch.HasCaught()) {
        // Hm, exception was thrown invoking callback. Boo.
        NPE_ERROR("! exception thrown while invoking callback");
        tryCatch.Reset();  // Swallow it up.
      }
    } else {
      // This was a fire-and-forget request.  Clean up!
      delete this;
    }
  }

 protected:
  // This is the actual implementation of the PHP-side "work".
  virtual void InPhp(PhpObjectMapper *m TSRMLS_DC) = 0;

 private:
  Nan::Callback *callback_;
  bool is_sync_;
};

// This is a message constructed in PHP, where the request is handled
// in JS and the response is handled in PHP.
class MessageToJs : public Message {
 public:
  // Constructed in PHP thread. The php_callback may be nullptr for
  // fire-and-forget methods.  If provided, it will be invoked
  // as a closure, with the exception as the first arg and the
  // return value as the second.  The MessageToJs will ref the
  // zval and unref it after use.  For sync calls, the php_callback
  // should be null and is_sync should be true.
  MessageToJs(ObjectMapper *m, zval *php_callback, bool is_sync)
      : Message(m), php_callback_(php_callback ZEND_FILE_LINE_CC),
    is_sync_(is_sync), js_callback_data_(nullptr), local_flag_ptr_(nullptr),
        stashedChannel_(nullptr) {
    assert(is_sync ? php_callback_.IsNull() : true);
  }
  virtual ~MessageToJs() {}
  inline bool IsSync() { return is_sync_; }
  // This is the "request" portion of the message, executed on the
  // JS side and sending its result back to PHP.
  // A HandleScope will have already been set up for us.
  void ExecuteJs(PhpMessageChannel *channel, bool no_async) override {
    // Keep a pointer to local stack space so that recursive invocations
    // can signal us without touching the message object. (See below.)
    bool local_flag = false, *local_flag_ptr =
      local_flag_ptr_ ? local_flag_ptr_ :
      (local_flag_ptr_ = &local_flag);
    TRACEX(">%s", js_callback_data_ ? " made callback" : "");
    if (mapper_->IsValid() && !js_callback_data_) {
      Nan::TryCatch tryCatch;
      // We might call MakeCallback while executing InJs, and then the client
      // code may execute the callback synchronously, which would mean that
      // we'd invoke CallbackFunction_ and re-enter ExecuteJS before returning
      // from InJs! And not just that, but the recursive ExecuteJs will have
      // sent a message to PHP, so the message itself could be deallocated
      // before we return.  Stash the channel before invoking InJs() and use
      // the local_flag_ptr to bail out without touching `this` again.
      stashedChannel_ = channel;
      InJs(mapper_);
      if (*local_flag_ptr) {
        // Already handled, bail (without touching `this`!)
        tryCatch.Reset();
        TRACE("< already handled");
        return;
      }
      // We're out of danger now; reset local_flag_ptr_ so that any async
      // exeuctions of ExecuteJs don't use our stack pointer after it's popped.
      local_flag_ptr_ = nullptr;
      // If an exception was thrown, set exception_
      if (tryCatch.HasCaught()) {
        exception_.Set(mapper_, tryCatch.Exception());
        tryCatch.Reset();
      }
      // If we made an async callback and didn't throw a sync exception,
      // we're done now; we'll do the rest of this function from the callback.
      if (js_callback_data_ && exception_.IsEmpty()) {
        // If we made an async callback but we're in a sync-only context,
        // throw an exception to break the deadlock
        if (no_async) {
          TRACE("- made callback but throwing deadlock exception");
          retval_.SetEmpty();
          exception_.Set(mapper_, Nan::TypeError("deadlock"));
        } else {
          TRACE("< made callback");
          return;
        }
      }
    }
    if (retval_.IsEmpty() && exception_.IsEmpty()) {
      // If no result, throw an exception.
      const char *msg = (!mapper_->IsValid()) ? "shutdown" :
        (!IsEmptyRetvalOk()) ? "no return value" : nullptr;
      if (msg) {
        exception_.Set(mapper_, Nan::TypeError(msg));
      }
    }
    // Now send response back to PHP side.
    // (Even for fire-and-forget messages, we want to execute the
    // destructor in the same thread as the constructor, so that
    // means we need to do a context switch back to PHP.)
    TRACEX("- sending response to PHP; JsCallbackData=%p", js_callback_data_);
    if (js_callback_data_) {
      js_callback_data_->MarkHandled();
      *local_flag_ptr = true;
    }
    MessageFlags flags = MessageFlags::ASYNC | MessageFlags::RESPONSE;
    if (IsShutdown()) { flags = flags | MessageFlags::SHUTDOWN; }
    channel->SendToPhp(this, flags);
    // Note that as soon as we call SendToPhp we can't touch `this` anymore,
    // since the PHP side may have deallocated the message.
    TRACE("<");
  }
  // This is the "response" portion of the message, executed on the
  // PHP side to dispatch results and/or cleanup.
  void ExecutePhp(JsMessageChannel *channel TSRMLS_DC) override {
    processed_ = true;
    if (is_sync_) {
      return;  // Caller will handle return value & exceptions.
    }
    // Ensure we always invoke ToPhp on the results, even for fire-and-forget.
    // This ensures that any JS objects sent from JS to PHP get instantiated
    // on the PHP side, so that their PHP gc destructors can eventually free
    // the object from the JS side.
    ZVal r{ZEND_FILE_LINE_C}, e{ZEND_FILE_LINE_C};
    if (HasException()) {
      exception_.ToPhp(mapper_, e TSRMLS_CC);
    } else {
      retval_.ToPhp(mapper_, r TSRMLS_CC);
    }
    if (!php_callback_.IsNull()) {
      // This case would be taken if we were invoking a sync JS method
      // asynchronously from the PHP side.
      ZVal closureRetval{ZEND_FILE_LINE_C};
      // Use plain zval to avoid allocating copy of method name.
      zval method; ZVAL_STRINGL(&method, "call", 4, 0);
      zval *args[] = { e.Ptr(), r.Ptr() };
      if (FAILURE == call_user_function(EG(function_table),
                                        php_callback_.PtrPtr(), &method,
                                        closureRetval.Ptr(), 2, args
                                        TSRMLS_CC)) {
        // Oh, well.  Ignore this.
        NPE_ERROR("! failure invoking closure");
      }
    } else {
      // This was a fire-and-forget request.  Clean up!
      delete this;
    }
  }

 protected:
  // This is the actual implementation of the JS-side "work".
  virtual void InJs(JsObjectMapper *m) = 0;
  // Override this for a "shutdown" message, which will close
  // the response queue after the response is sent.
  virtual bool IsShutdown() { return false; }
  // This allows invoking async methods on the JS side.
  v8::Local<v8::Function> MakeCallback() {
    TRACE(">");
    Nan::EscapableHandleScope scope;
    js_callback_data_ = new JsCallbackData(this);
    TRACEX("- JsCallbackData=%p", js_callback_data_);
    v8::Local<v8::Function> cb = Nan::New<v8::Function>(
      CallbackFunction_, Nan::New<v8::External>(js_callback_data_));
    js_callback_data_->Wrap(cb);
    TRACE("<");
    return scope.Escape(cb);
  }

 private:
  class JsCallbackData {
   public:
    explicit JsCallbackData(MessageToJs *msg)
        : handle_(), msg_(msg), is_handled_(false) {
    }
    virtual ~JsCallbackData() { }
    void Wrap(v8::Local<v8::Function> cb) {
      // Associate with cb; this will be freed when made weak & cb is gc'ed.
      handle_.Reset(cb);
    }
    void MarkHandled() {
      TRACEX("> %p", this);
      assert(!is_handled_);
      is_handled_ = true;
      handle_.SetWeak<JsCallbackData>(this, Destroy_,
                                      Nan::WeakCallbackType::kParameter);
      TRACEX("< %p", this);
    }
    inline MessageToJs *msg() { return msg_; }
    inline bool is_handled() { return is_handled_; }

   private:
    static void Destroy_(const Nan::WeakCallbackInfo<JsCallbackData>& data) {
      TRACE(">");
      JsCallbackData *obj = data.GetParameter();
      delete obj;
      TRACE("<");
    }
    NAN_DISALLOW_ASSIGN_COPY_MOVE(JsCallbackData)
    Nan::Persistent<v8::Function> handle_;
    MessageToJs *msg_;
    bool is_handled_;
  };
  static void CallbackFunction_(
      const Nan::FunctionCallbackInfo<v8::Value>& info) {
      // XXX we really need out own piece of memory here, so that
      // we can detect if the callback is called multiple times,
      // or if it is invoked after the original method throws an
      // error. XXX
      // Set retval_ and exception_ based on function parameters,
      // then invoke Execute() again.
      TRACE(">");
      JsCallbackData *data = reinterpret_cast<JsCallbackData*>
          (info.Data().As<v8::External>()->Value());
      TRACEX("- JsCallbackData=%p", data);
      if (data->is_handled()) {
        // not safe to touch msg, it may have been deallocated.
        TRACE("< already handled");
        return;
      }
      MessageToJs *msg = data->msg();
      v8::Local<v8::Value> exception = (info.Length() > 0) ? info[0] :
          static_cast<v8::Local<v8::Value>>(Nan::Undefined());
      if (exception->IsNull() || exception->IsUndefined()) {
          v8::Local<v8::Value> retval = (info.Length() > 1) ? info[1] :
              static_cast<v8::Local<v8::Value>>(Nan::Undefined());
          msg->retval_.Set(msg->mapper_, retval);
      } else {
          msg->exception_.Set(msg->mapper_, exception);
      }
      assert(msg->js_callback_data_);
      msg->ExecuteJs(msg->stashedChannel_, false);
      TRACE("<");
  }

  ZVal php_callback_;
  bool is_sync_;
  JsCallbackData *js_callback_data_;
  bool *local_flag_ptr_;
  PhpMessageChannel *stashedChannel_;
};

}  // namespace node_php_embed

#endif  // NODE_PHP_EMBED_MESSAGES_H_

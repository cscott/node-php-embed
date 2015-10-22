#ifndef NODE_PHP_MESSAGES_H
#define NODE_PHP_MESSAGES_H
#include <iostream>
#include "values.h"

namespace node_php_embed {

class Message;

// Interfaces for sending messages to different threads.
class JsMessageChannel {
 public:
  virtual ~JsMessageChannel() { }
  // if isSync is true, will not return until response has been received.
  virtual void SendToJs(Message *m, bool isSync TSRMLS_DC) const = 0;
};
class PhpMessageChannel {
 public:
  virtual ~PhpMessageChannel() { }
  // if isSync is true, will not return until response has been received.
  virtual void SendToPhp(Message *m, bool isSync) const = 0;
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

  // We don't know which of these is the "request" or "response" part
  // yet, but we'll name them by execution context, and we'll
  // provide them with access to the message channel so they can
  // send responses to the other side (if they happen to be the
  // request context).
  virtual void ExecutePhp(JsMessageChannel *channel TSRMLS_DC) = 0;
  virtual void ExecuteJs(PhpMessageChannel *channel) = 0;

 protected:
  ObjectMapper *mapper_;
  Value retval_, exception_;
  bool processed_;
};

// This is a message constructed in JS, where the request is handled
// in PHP and the response is handled in JavaScript.
class MessageToPhp : public Message {
 public:
  // Constructed in JS thread.  The callback may be NULL for
  // fire-and-forget methods.  If provided, it will be invoked
  // nodejs-style, with exception as first arg and retval as second.
  // The callback will be owned by the message, and deleted after use.
  // For sync calls, the callback should be null and isSync should be true.
  MessageToPhp(ObjectMapper *m, Nan::Callback *callback, bool isSync)
      : Message(m), callback_(callback), isSync_(isSync) {
    assert(isSync ? !callback : true);
  }
  virtual ~MessageToPhp() {
    if (callback_) { delete callback_; }
  }
  inline bool isSync() { return isSync_; }
  // This is the "request" portion of the message, executed on the PHP
  // side and sending its result back to JS.
  void ExecutePhp(JsMessageChannel *channel TSRMLS_DC) {
    if (mapper_->IsValid()) {
      zend_try {
        InPhp(mapper_ TSRMLS_CC);
      } zend_catch {
        if (EG(exception)) {
          exception_.Set(mapper_, EG(exception) TSRMLS_CC);
          zend_clear_exception(TSRMLS_C);
        }
      } zend_end_try();
    }
    if (retval_.IsEmpty() && exception_.IsEmpty()) {
      // if no result, throw an exception
      const char *msg = mapper_->IsValid() ? "no return value" : "shutdown";
      exception_.SetString(msg, strlen(msg));
    }
    // Now send response back to JS side.
    // (Even for fire-and-forget messages, we want to execute the
    // destructor in the same thread as the constructor, so that
    // means we need to do a context switch back to JS.)
    channel->SendToJs(this, false /*don't block!*/ TSRMLS_CC);
  }
  // This is the "response" portion of the message, executed on the JS
  // side to dispatch results and/or cleanup.
  void ExecuteJs(PhpMessageChannel *channel) {
    processed_ = true;
    if (isSync_) {
      return; // caller will handle return value & exceptions.
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
        // hm, exception was thrown invoking callback. Boo.
        NPE_ERROR("! exception thrown while invoking callback");
        tryCatch.Reset(); // swallow it up.
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
  bool isSync_;
};

// This is a message constructed in PHP, where the request is handled
// in JS and the response is handled in PHP.
class MessageToJs : public Message {
 public:
  // Constructed in PHP thread. The callback may be NULL for
  // fire-and-forget methods.  If provided, it will be invoked
  // as a closure, with the exception as the first arg and the
  // return value as the second.  The MessageToJs will ref the
  // zval and unref it after use.  For sync calls, the callback
  // should be null and isSync should be true.
  MessageToJs(ObjectMapper *m, zval *callback, bool isSync)
      : Message(m), callback_(callback ZEND_FILE_LINE_CC), isSync_(isSync) {
    assert(isSync ? callback_.IsNull() : true);
  }
  virtual ~MessageToJs() {}
  inline bool isSync() { return isSync_; }
  // This is the "request" portion of the message, executed on the
  // JS side and sending its result back to PHP.
  void ExecuteJs(PhpMessageChannel *channel) {
    Nan::HandleScope scope;
    if (mapper_->IsValid()) {
      Nan::TryCatch tryCatch;
      // xxx InJs should be allowed to be async; perhaps it should
      // return a promise in that case.
      InJs(mapper_);
      if (tryCatch.HasCaught()) {
        // If an exception was thrown, set exception_
        exception_.Set(mapper_, tryCatch.Exception());
        tryCatch.Reset();
      }
    }
    if (retval_.IsEmpty() && exception_.IsEmpty()) {
      // if no result, throw an exception
      const char *msg = mapper_->IsValid() ? "no return value" : "shutdown";
      exception_.Set(mapper_, Nan::TypeError(msg));
    }
    // Now send response back to PHP side.
    // (Even for fire-and-forget messages, we want to execute the
    // destructor in the same thread as the constructor, so that
    // means we need to do a context switch back to PHP.)
    channel->SendToPhp(this, false /*don't block!*/);
  }
  // This is the "response" portion of the message, executed on the
  // PHP side to dispatch results and/or cleanup.
  void ExecutePhp(JsMessageChannel *channel TSRMLS_DC) {
    processed_ = true;
    if (isSync_) {
      return; // caller will handle return value & exceptions.
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
    if (!callback_.IsNull()) {
      ZVal closureRetval{ZEND_FILE_LINE_C};
      // use plain zval to avoid allocating copy of method name
      zval method; ZVAL_STRINGL(&method, "call", 4, 0);
      zval *args[] = { e.Ptr(), r.Ptr() };
      if (FAILURE == call_user_function(EG(function_table),
                                        callback_.PtrPtr(), &method,
                                        closureRetval.Ptr(), 2, args
                                        TSRMLS_CC)) {
        // oh, well.  ignore this.
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
 private:
  ZVal callback_;
  bool isSync_;
};

// example of MessageToPhp
class PhpGetPropertyMsg : public MessageToPhp {
 public:
  PhpGetPropertyMsg(ObjectMapper *m, Nan::Callback *callback, bool isSync,
                    v8::Local<v8::Value> obj, v8::Local<v8::Value> name)
      : MessageToPhp(m, callback, isSync),
        obj_(m, obj), name_(m, name) { }
 protected:
  virtual void InPhp(PhpObjectMapper *m TSRMLS_DC) {
    ZVal obj{ZEND_FILE_LINE_C}, name{ZEND_FILE_LINE_C};
    zval *r;
    zend_class_entry *ce;
    zend_property_info *property_info;

    obj_.ToPhp(m, obj TSRMLS_CC); name_.ToPhp(m, name TSRMLS_CC);
    if (!(obj.IsObject() && name.IsString())) {
      retval_.SetNull();
      return;
    }
    ce = Z_OBJCE_P(*obj);
    property_info = zend_get_property_info(ce, *name, 1 TSRMLS_CC);
    if (property_info && property_info->flags & ZEND_ACC_PUBLIC) {
      r = zend_read_property(NULL, *obj, Z_STRVAL_P(*name), Z_STRLEN_P(*name), true TSRMLS_CC);
      // special case uninitialized_zval_ptr and return an empty value
      // (indicating that we don't intercept this property) if the
      // property doesn't exist.
      if (r == EG(uninitialized_zval_ptr)) {
        // XXX this is going to trigger an exception.
        retval_.SetEmpty();
        return;
      } else {
        retval_.Set(m, r TSRMLS_CC);
        /* We don't own the reference to php_value... unless the
         * returned refcount was 0, in which case the below code
         * will free it. */
        zval_add_ref(&r);
        zval_ptr_dtor(&r);
        return;
      }
    }
    // XXX fallback to __get method
    retval_.SetNull();
  }
 private:
  Value obj_;
  Value name_;
};

} /* namespace */

#endif

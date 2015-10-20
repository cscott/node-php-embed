#ifndef POORWEAKMAP_H
#define POORWEAKMAP_H
#include <nan.h>

/* Poor man's weak map. */

class PoorWeakMap {
 public:
    PoorWeakMap(v8::Isolate *isolate) {
        int myid = id_++;
        wmkey_.Reset(v8::String::Concat(
          NEW_STR("php::poorweakmap::"),
          Nan::To<v8::String>(Nan::New(myid)).ToLocalChecked()));
    }
    virtual ~PoorWeakMap() { wmkey_.Reset(); }
    void Set(Local<Object> key, Local<Value> value) {
        Nan::HandleScope scope;
        return key->SetHiddenValue(Nan::New(wmkey_), value);
    }
    bool Has(Local<Object> key) {
        Nan::HandleScope scope;
        Local<Value> r = key->GetHiddenValue(Nan::New(wmkey_));
        return !r.IsEmpty();
    }
    bool Delete(Local<Object> key) {
        Nan::HandleScope scope;
        return key->DeleteHiddenValue(Nan::New(wmkey_));
    }
    Local<Value> Get(Local<Object> key) {
        Nan::EscapableHandleScope scope;
        return scope.Escape(key->GetHiddenValue(Nan::New(wmkey_)));
    }
 private:
    static int id_ = 0;
    Nan::Persistent<v8::String> wmkey_;
    }

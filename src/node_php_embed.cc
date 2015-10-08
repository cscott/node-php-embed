#include <nan.h>
#include <sapi/embed/php_embed.h>
#include <Zend/zend_exceptions.h>
#include "macros.h"

static void ModuleShutdown(void *arg);

NAN_METHOD(eval) {
    TSRMLS_FETCH();

    REQUIRE_ARGUMENT_STRING(0, source);
    if (!*source) {
        Nan::ThrowSyntaxError("bad string");
        return;
    }

    zval *retval;
    ALLOC_INIT_ZVAL(retval);
    zend_first_try {
        if (FAILURE == zend_eval_string_ex(*source, retval, "eval", true)) {
            if (EG(exception)) {
                zend_clear_exception(TSRMLS_C);
                ZVAL_STRING(retval, "<threw exception>", 1);
            } else {
                ZVAL_STRING(retval, "<eval failure>", 1);
            }
        }
        convert_to_string(retval);
        info.GetReturnValue().Set(
            Nan::New<v8::String>(Z_STRVAL_P(retval), Z_STRLEN_P(retval))
            .ToLocalChecked()
        );
    } zend_catch {
        info.GetReturnValue().Set(NEW_STR("<bailout>"));
    } zend_end_try();
    zval_dtor(retval);
}

NAN_MODULE_INIT(ModuleInit) {
    TSRMLS_FETCH();
    char *argv[] = { };
    int argc = 0;
    php_embed_module.php_ini_ignore = true;
    php_embed_init(argc, argv PTSRMLS_CC);
    node::AtExit(ModuleShutdown, NULL);
    Nan::Set(target, NEW_STR("eval"),
             Nan::GetFunction(Nan::New<v8::FunctionTemplate>(eval)).ToLocalChecked());
}

void ModuleShutdown(void *arg) {
    TSRMLS_FETCH();
    php_embed_shutdown(TSRMLS_CC);
}


NODE_MODULE(php_embed, ModuleInit)

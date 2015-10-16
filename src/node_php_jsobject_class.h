#ifndef NODE_PHP_OBJECT_CLASS_H
#define NODE_PHP_OBJECT_CLASS_H

#include <nan.h>
extern "C" {
#include "php.h"
#include "Zend/zend.h"
}

namespace node_php_embed {

class JsMessageChannel;
class ObjectMapper;

struct node_php_jsobject {
    zend_object std;
    JsMessageChannel *channel;
    uint32_t id;
};

/* Create a PHP proxy for a JS object */
void node_php_jsobject_create(zval *res, ObjectMapper *mapper, uint32_t id TSRMLS_DC);

}

extern zend_class_entry *php_ce_jsobject;

PHP_MINIT_FUNCTION(node_php_jsobject_class);

#endif

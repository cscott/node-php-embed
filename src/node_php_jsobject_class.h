#ifndef NODE_PHP_OBJECT_CLASS_H
#define NODE_PHP_OBJECT_CLASS_H

struct node_php_jsobject {
    zend_object std;
    v8::Persistent<v8::Value> v8obj;
};

extern zend_class_entry *php_ce_jsobject;

/* Create a PHP proxy for a JS object */
void node_php_jsobject_create(zval *, v8::Handle<v8::Value>, int, v8::Isolate * TSRMLS_DC);

PHP_MINIT_FUNCTION(node_php_jsobject_class);

#endif

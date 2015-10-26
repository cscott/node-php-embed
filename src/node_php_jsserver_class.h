// This is a thin wrapper object to allow javascript to call
// php_register_variable_* inside an initialization callback.

// Copyright (c) 2015 C. Scott Ananian <cscott@cscott.net>
#ifndef NODE_PHP_EMBED_NODE_PHP_JSSERVER_CLASS_H_
#define NODE_PHP_EMBED_NODE_PHP_JSSERVER_CLASS_H_

extern "C" {
#include "main/php.h"
#include "Zend/zend.h"
}

namespace node_php_embed {

struct node_php_jsserver {
  zend_object std;
  zval *track_vars_array;
};

/* Create a PHP proxy for php_register_variable_*. */
void node_php_jsserver_create(zval *res, zval *track_vars_array TSRMLS_DC);

}  // namespace node_php_embed

extern zend_class_entry *php_ce_jsserver;

PHP_MINIT_FUNCTION(node_php_jsserver_class);

#endif  // NODE_PHP_EMBED_NODE_PHP_JSSERVER_CLASS_H_

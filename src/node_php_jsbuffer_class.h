#ifndef NODE_PHP_JSBUFFER_CLASS_H
#define NODE_PHP_JSBUFFER_CLASS_H

extern "C" {
#include <main/php.h>
#include <Zend/zend.h>
}

namespace node_php_embed {

struct node_php_jsbuffer {
    zend_object std;
    const char *data;
    ulong length;
    int owner; /* 0 = not owned, 1 = php owned, 2 = c++ owned */
    zval *z; /* can hold a reference to a PHP string */
};

/* Create a PHP version of a JS Buffer.
 * owner=0 - not owned
 * owner=1 - PHP owned
 * owner=2 - C++ owned
 */
void node_php_jsbuffer_create(zval *res, const char *data, ulong length, int owner TSRMLS_DC);

}

extern zend_class_entry *php_ce_jsbuffer;

PHP_MINIT_FUNCTION(node_php_jsbuffer_class);

#endif

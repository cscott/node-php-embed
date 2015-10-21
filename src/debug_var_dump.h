#ifndef DEBUG_VAR_DUMP_H
#define DEBUG_VAR_DUMP_H
extern "C" {
#include <main/php.h>
#include <ext/standard/php_var.h>
}

/* Hack with PHP's buffering to bypass it and vardump directly to stderr. */
static void debug_var_dump_handler(char *output, uint output_len, char **handled_output, uint *handled_output_len, int mode TSRMLS_DC) {
    // dump this right to stderr
    fwrite(output, 1, output_len, stderr);
    *handled_output = estrdup("");
    *handled_output_len = 0;
}

static inline void debug_var_dump(zval *v TSRMLS_DC) {
    // first push a new buffer context
    php_output_start_internal("debug_var_dump", 14, debug_var_dump_handler, 64, PHP_OUTPUT_HANDLER_STDFLAGS TSRMLS_CC);
    // now invoke var_dump
    php_var_dump(&v, 1 TSRMLS_CC);
    // now flush/pop the buffer context
    php_output_end(TSRMLS_C);
}
#endif

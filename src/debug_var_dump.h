// Helper for debugging PHP values from the PHP thread, without invoking
// our thunk to pipe the output to JS.  Bypasses the output of var_dump
// directly to stderr.

// Copyright (c) 2015 C. Scott Ananian <cscott@cscott.net>
#ifndef NODE_PHP_EMBED_DEBUG_VAR_DUMP_H_
#define NODE_PHP_EMBED_DEBUG_VAR_DUMP_H_

extern "C" {
#include "main/php.h"
#include "ext/standard/php_var.h"
}

/* Hack with PHP's buffering to bypass it and vardump directly to stderr. */
static void debug_var_dump_handler(char *output, uint output_len,
                                   char **handled_output,
                                   uint *handled_output_len,
                                   int mode TSRMLS_DC) {
  // dump this right to stderr
  fwrite(output, 1, output_len, stderr);
  *handled_output = estrdup("");
  *handled_output_len = 0;
}

static inline void debug_var_dump(zval *v TSRMLS_DC) {
  // first push a new buffer context
  const char *context_name = "debug_var_dump";
  php_output_start_internal(context_name, strlen(context_name),
                            debug_var_dump_handler, 64/*chunk size*/,
                            PHP_OUTPUT_HANDLER_STDFLAGS TSRMLS_CC);
  // now invoke var_dump
  php_var_dump(&v, 1 TSRMLS_CC);
  // now flush/pop the buffer context
  php_output_end(TSRMLS_C);
}

#endif  // NODE_PHP_EMBED_DEBUG_VAR_DUMP_H_

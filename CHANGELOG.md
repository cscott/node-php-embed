# php-embed x.x.x (not yet released)
* Add synchronous PHP access to JavaScript variables, functions, and
  methods.
* Wrap JavaScript Buffer objects so they can be passed from PHP to JS.
  This allows us to avoid decoding/re-encoding UTF-8 when we stream
  data back from PHP.
* Node >= 2.4.0 is now required, due to use of C++11 features and
  v8's `NativeWeakMap`.

# php-embed 0.0.1 (2015-10-10)
* Initial release: just a basic async invocation of a PHP request,
  with streaming back to node.

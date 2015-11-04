# php-embed x.x.x (not yet released)
* Add and enable Opcache extension for opcode caching (performance).
* Add and enable Intl extension (fast internationalization support).
* Add and enable APCu extension for object caching (performance).

# php-embed 0.5.2 (2015-11-03)
* Ensure PHP private properties aren't writable.
* Implement `__toString` magic method on wrapped JavaScript objects.
* Include stack trace when JavaScript exceptions are thrown into PHP.
* Implement wrapping of PHP arrays and objects implementing
  `ArrayAccess` and `Countable`.
* Add `Js\ByRef` to allow arrays to be passed by reference to
  JavaScript functions.
* Add binaries for node 5.0.0.

# php-embed 0.5.1 (2015-10-29)
* Support passing cookies and POST data to PHP request.

# php-embed 0.5.0 (2015-10-28)
* Support server variables, headers, and query string
  processing to allow using the embedded PHP to process http
  requests from node's http server.
* Support passing "command-line arguments" to allow the embedded PHP
  to execute scripts using PHP's CLI interface.
* Add a simple `php-embed` CLI script for easy testing.
* Allow PHP to invoke asynchronous JavaScript functions synchronously
  by passing a `Js\Wait` object where the callback would go.  This
  blocks the PHP event loop (naturally) but not the JavaScript one.
  This is used internally to implement PHP requests to flush the
  output stream.
* Wrap PHP objects (but not yet arrays) for use within Node.
  Property access is implemented; method invocation is not yet
  implemented.
* Rework message passing to allow two-way communication between JS
  and PHP.  Both JS and PHP have event loops now, and both can do
  asynchronous method calls (but at the moment most calls are
  synchronous).
* Aggressively lint the C++ and JS code bases.

# php-embed 0.0.2 (2015-10-21)
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

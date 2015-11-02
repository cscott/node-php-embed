'use strict';
var binary = require('node-pre-gyp');
var path = require('path');
var bindingPath =
  binary.find(path.resolve(path.join(__dirname, '..', 'package.json')));
var bindings = require(bindingPath);
bindings.setIniPath(path.join(__dirname, 'php.ini'));
bindings.setStartupFile(path.join(__dirname, 'startup.php'));

var packageJson = require('../package.json');
var Promise = require('prfun');
var url = require('url');

var request = Promise.promisify(bindings.request);

// Hacky way to make strings safe for eval.
var addslashes = function(s) {
  return "'" + s.replace(/[\'\\\x00]/g, function(c) {
    if (c === '\x00') {
      return '\'."\0".\'';
    }
    return '\\' + c;
  }) + "'";
};

exports.PhpObject = bindings.PhpObject;

// We write 0-length buffers to the stream and attach a callback
// to implement "flush".  However, not all streams actually
// support this -- in particular, HTTP streams will never fire
// the callback if the input is 0 length (boo!).  So we'll wrap
// the passed-in stream and do a little bit of bookkeeping to
// ensure this always works right.
// While we're at it, we'll do some hand-holding of the
// HTTP header API as well, as well as support reading
// POST data from an input stream.
// NOTE that this is not actually a node.js "WritableStream" any more;
// this is just an internal interface we can pass over to the PHP side.
var StreamWrapper = function(inStream, outStream) {
  this._initWrite(outStream);
  this._initHeader(outStream);
  this._initRead(inStream);

};

// WRITE interface
StreamWrapper.prototype._initWrite = function(outStream) {
  this.stream = outStream;
  this.flushed = true;
  this.error = null;
  this.callbacks = [];
  this.stream.on('drain', this._onDrain.bind(this));
  this.stream.on('error', this._onError.bind(this));
  this.stream.on('close', this._onClose.bind(this));
  this.stream.on('finish', this._onFinish.bind(this));
};
StreamWrapper.prototype.write = function(buffer, cb) {
  if (this.error) {
    if (cb) { setImmediate(function() { cb(this.error); }); }
    return true;
  }
  var notBuffered = (buffer.length === 0) ? this.flushed :
      this.stream.write(buffer);
  if (notBuffered) {
    this.flushed = true;
    if (cb) { setImmediate(cb); }
  } else {
    this.flushed = false;
    if (cb) { this.callbacks.push(cb); }
  }
  return notBuffered;
};
StreamWrapper.prototype._onDrain = function() {
  this.flushed = true;
  this.callbacks.forEach(function(f) { setImmediate(f); });
  this.callbacks.length = 0;
};
StreamWrapper.prototype._onError = function(e) {
  this.error = e;
  this._onDrain();
};
StreamWrapper.prototype._onClose = function() {
  this._onError(new Error('stream closed'));
};
StreamWrapper.prototype._onFinish = function() {
  this._onError(new Error('stream finished'));
};

// HEADER interface
StreamWrapper.prototype._initHeader = function(outStream) {
  this.supportsHeaders = (
    typeof (this.stream.getHeader) === 'function' &&
    typeof (this.stream.setHeader) === 'function'
  );
};
StreamWrapper.prototype.sendHeader = function(headerBuf) {
  if (!this.supportsHeaders) { return; }
  if (headerBuf === null) { return; }  // This indicates the "last header".
  var header;
  try {
    // Headers are sent as Buffer from PHP to JS to avoid re-encoding
    // in transit.  But node.js wants strings, so we need to do
    // some decoding.  Technically headers are ISO-8859-1 encoded,
    // with a strong recommendation to only use ASCII.
    // See RFC 2616, https://tools.ietf.org/html/rfc7230#section-3.2.4
    header = headerBuf.toString('ascii');
  } catch (e) {
    console.error('BAD HEADER ENCODING, SKIPPING:', headerBuf);
    return;
  }
  var m = /^HTTP\/(\d+\.\d+) (\d+)( (.*))?$/.exec(header);
  if (m) {
    this.stream.statusCode = parseInt(m[2], 10);
    if (m[4]) {
      this.stream.statusMessage = m[4];
    }
    return;
  }
  m = /^([^: ]+): (.*)$/.exec(header);
  if (m) {
    // Some headers we are allowed to emit more than once;
    // node.js wants an array of values in that case.
    var name = m[1];
    var newValue = m[2];
    var old = this.stream.getHeader(name);
    if (old) {
      if (!Array.isArray(old)) { old = [old]; }
      newValue = old.concat(newValue);
    }
    this.stream.setHeader(name, newValue);
    return;
  }
  console.error('UNEXPECTED HEADER, SKIPPING:', header);
};

// READ interface
StreamWrapper.prototype._initRead = function(inStream) {
  this.inputStream = inStream;
  this.inputSize = 0;
  this.inputResult = null;
  this.inputComplete = null;
  this.inputLeftover = null;
  this.inputCallbacks = []; // Future read requests.
  this.inputError = null;
  this.inputEnd = false;

  if (!this.inputStream) {
    this.inputEnd = true;
  } else {
    this.inputStream.on('data', this._onInputData.bind(this, false));
    this.inputStream.on('end', this._onInputEnd.bind(this));
    this.inputStream.on('error', this._onInputError.bind(this));
    this.inputStream.pause();
  }
};
StreamWrapper.prototype.read = function(size, cb) {
  var self = this;
  var error = this.inputError;
  if (this.inputResult !== null) {
    // Read already in progress, queue for later.
    this.inputCallbacks.push(function() { self.read(size, cb); });
    return;
  }
  if (this.inputEnd || error) {
    if (cb) { setImmediate(function() { cb(error, new Buffer(0)); }); }
    return;
  }
  this.inputResult = new Buffer(size);
  this.inputSize = 0;
  this.inputComplete = cb;
  if (this.inputLeftover || size === 0) {
    this._onInputData(false, this.inputLeftover || new Buffer(0));
  } else {
    // Enable data events.
    this.inputStream.resume();
  }
};
StreamWrapper.prototype._onInputData = function(isEnd, buffer) {
  this.inputStream.pause();
  var remaining = (this.inputResult.length - this.inputSize);
  var amt = Math.min(buffer.length, remaining);
  buffer.copy(this.inputResult, this.inputSize, 0, amt);
  this.inputSize += amt;
  // Are we done with this input request?
  if (this.inputSize === this.inputResult.length || isEnd) {
    var cb = this.inputComplete;  // Capture this for callback
    var err = this.inputError;    // Capture this for callback
    var result = this.inputResult.slice(0, this.inputSize);
    setImmediate(function() { cb(err, result); });  // Queue callback
    this.inputResult = this.inputComplete = null;
    this.inputEnd = isEnd;
    // Are we done with this buffer?
    this.inputLeftover = (amt < buffer.length) ? buffer.slice(amt) : null;
    // Were there any more reads waiting?
    if (this.inputCallbacks.length > 0) {
      setImmediate(this.inputCallbacks.shift());
    }
  } else {
    // Need more chunks!
    this.inputLeftover = null;
    this.inputStream.resume();
  }
};
StreamWrapper.prototype._onInputEnd = function() {
  if (this.inputResult) {
    this._onInputData(true, new Buffer(0));
  } else {
    this.inputEnd = true;
  }
  while (this.inputCallbacks.length > 0) {
    setImmediate(this.inputCallbacks.shift());
  }
};
StreamWrapper.prototype._onInputError = function(e) {
  this.inputError = e;
  this._onInputEnd();
};


exports.request = function(options, cb) {
  options = options || {};
  var source = options.source;
  if (options.file) {
    source = 'require ' + addslashes(options.file) + ';';
  }
  var stream = new StreamWrapper(options.request,
                                 options.stream || process.stdout);
  var buildServerVars = function() {
    var server = Object.create(null);
    server.CONTEXT = options.context;
    if (options.file) {
      server.PHP_SELF = options.file;
      server.SCRIPT_FILENAME = options.file;
    }
    if (options.request) {
      var headers = options.request.headers || {};
      Object.keys(headers).forEach(function(h) {
        var hh = 'HTTP_' + h.toUpperCase().replace(/[^A-Z]/g, '_');
        // The array case is very unusual here: it should basically
        // only occur for Set-Cookie, which isn't going to be sent
        // *to* PHP.  But make sure we don't crash if it is.
        if (Array.isArray(headers[h])) { return; }
        server[hh] = headers[h];
      });
      server.PATH = process.env.PATH;
      server.SERVER_SIGNATURE = '<address>' +
        '<a href="' + packageJson.homepage + '">' + packageJson.name +
        '</a>: ' + packageJson.description + '</address>';
      server.SERVER_SOFTWARE =
        packageJson.name + ' ' + packageJson.version;
      server.SERVER_PROTOCOL =
        'HTTP/' + options.request.httpVersion;
      server.GATEWAY_INTERFACE =
        'CGI/1.1';
      server.REQUEST_SCHEME =
        'http';  // XXX Is it possible to determine this from req object?
      server.REQUEST_METHOD =
        options.request.method;
      server.REQUEST_URI =
        options.request.url;
      server.SERVER_ADMIN =
        'webmaster@localhost';  // Bogus value: user can override.
      var parsedUrl = url.parse(options.request.url);
      server.QUERY_STRING =
        (parsedUrl.search || '').replace(/^[?]/, '');
      var socket = options.request.socket;
      if (socket) {
        server.REMOTE_ADDR = socket.remoteAddress;
        server.REMOTE_PORT = socket.remotePort;
        server.SERVER_ADDR = socket.localAddress;
        server.SERVER_PORT = socket.localPort;
      }
      server.DOCUMENT_ROOT =
        '/var/www';  // Bogus value: user can override.
    }
    if (options.serverInitFunc) {
      options.serverInitFunc(server);
    }
    return server;
  };
  var args = options.args || [];
  var serverVars = buildServerVars();
  // This function initializes $_SERVER inside the PHP request.
  var initServer = function(server, cb) {
    // This loop deliberately doesn't use "hasOwnProperty" in order to
    // allow serverVars to be inherited from an object with defaults.
    for (var f in serverVars) {
      server[f] = serverVars[f];
    }
    cb();
  };
  return request(source, stream, args, serverVars, initServer).tap(function() {
    // Ensure the stream is flushed before promise is resolved.
    return new Promise(function(resolve, reject) {
      stream.write(new Buffer(0), function(e) {
        if (e) { reject(e); } else { resolve(); }
      });
    });
  }).nodify(cb);
};

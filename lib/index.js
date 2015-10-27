var binary = require('node-pre-gyp');
var path = require('path');
var bindingPath =
  binary.find(path.resolve(path.join(__dirname, '..', 'package.json')));
var bindings = require(bindingPath);
bindings.setIniPath(path.join(__dirname, 'php.ini'));

var packageJson = require('../package.json');
var Promise = require('prfun');
var url = require('url');

var request = Promise.promisify(bindings.request);

// Hacky way to make strings safe for eval.
var addslashes = function(s) {
  return "'" + s.replace(/[\'\\\000]/g, function(c) {
    if (c === '\000') {
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
// HTTP header API as well.
// NOTE that this is not actually a node.js "WritableStream" any more;
// this is just an internal interface we can pass over to the PHP side.
var StreamWrapper = function(stream) {
  this.stream = stream;
  this.flushed = true;
  this.error = null;
  this.callbacks = [];
  this.supportsHeaders = (
    typeof (stream.getHeader) === 'function' &&
    typeof (stream.setHeader) === 'function'
  );
  stream.on('drain', this.onDrain.bind(this));
  stream.on('error', this.onError.bind(this));
  stream.on('close', this.onClose.bind(this));
  stream.on('finish', this.onFinish.bind(this));
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
StreamWrapper.prototype.onDrain = function() {
  this.flushed = true;
  this.callbacks.forEach(function(f) { setImmediate(f); });
  this.callbacks.length = 0;
};
StreamWrapper.prototype.onError = function(e) {
  this.error = e;
  this.onDrain();
};
StreamWrapper.prototype.onClose = function() {
  this.onError(new Error('stream closed'));
};
StreamWrapper.prototype.onFinish = function() {
  this.onError(new Error('stream finished'));
};
StreamWrapper.prototype.sendHeader = function(headerBuf) {
  if (!this.supportsHeaders) { return; }
  if (headerBuf === null) { return; }  // This indicates the "last header".
  var header;
  try {
    // Headers are sent from PHP to JS to avoid re-encoding, but
    // technically they are ISO-8859-1 encoded, with a strong
    // recommendation to only use ASCII.
    // See RFC 2616, https://tools.ietf.org/html/rfc7230#section-3.2.4
    header = headerBuf.toString('ascii');
  } catch (e) {
    console.error('BAD HEADER ENCODING, SKIPPING:', headerBuf);
    return;
  }
  var m = /^HTTP\/(\d+\.\d+) (\d+) (.*)$/.exec(header);
  if (m) {
    this.stream.statusCode = parseInt(m[2], 10);
    this.stream.statusMessage = m[3];
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

exports.request = function(options, cb) {
  options = options || {};
  var source = options.source;
  if (options.file) {
    source = 'require ' + addslashes(options.file) + ';';
  }
  var stream = new StreamWrapper(options.stream || process.stdout);
  var context = options.context;
  var args = options.args || [];
  // This function initializes $_SERVER inside the PHP request.
  var initServer = function(server, cb) {
    server.CONTEXT = context;
    if (options.file) {
      server.PHP_SELF = options.file;
      server.SCRIPT_FILENAME = options.file;
    }
    if (options.request) {
      var headers = options.request.headers || {};
      Object.keys(headers).forEach(function(h) {
        var hh = 'HTTP_' + h.toUpperCase().replace(/[^A-Z]/g, '_');
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
        'http'; // XXX?
      server.REQUEST_METHOD =
        options.request.method;
      server.REQUEST_URI =
        options.request.url;
      server.SERVER_ADMIN =
        'webmaster@localhost'; // XXX?
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
    }
    if (typeof (options.serverInitFunc) === 'function') {
      options.serverInitFunc(server, cb);
    } else {
      cb();
    }
  };
  return request(source, stream, args, initServer).tap(function() {
    // Ensure the stream is flushed before promise is resolved.
    return new Promise(function(resolve, reject) {
      stream.write(new Buffer(0), function(e) {
        if (e) { reject(e); } else { resolve(); }
      });
    });
  }).nodify(cb);
};

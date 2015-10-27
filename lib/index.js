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

exports.request = function(options, cb) {
  options = options || {};
  var source = options.source;
  if (options.file) {
    source = 'require ' + addslashes(options.file) + ';';
  }
  var stream = options.stream || process.stdout;
  var context = options.context;
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
    cb();
  };
  return request(source, stream, initServer).tap(function() {
    // Ensure the stream is flushed before promise is resolved.
    return new Promise(function(resolve, reject) {
      stream.write(new Buffer(0), resolve);
    });
  }).nodify(cb);
};

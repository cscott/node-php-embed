var binary = require('node-pre-gyp');
var path = require('path');
var Promise = require('prfun');
var binding_path =
  binary.find(path.resolve(path.join(__dirname, '..', 'package.json')));
var bindings = require(binding_path);
bindings.setIniPath(path.join(__dirname, 'php.ini'));

var request = Promise.promisify(bindings.request);

// Hacky way to make strings safe for eval.
var addslashes = function(s) {
	return "'" + s.replace(/[\'\\\000]/g, function(c) {
		if (c==='\000') {
			return '\'."\0".\'';
		}
		return '\\' + c;
	}) + "'";
};

exports.request = function(options, cb) {
	options = options || {};
	var source = options.source;
	if (options.file) {
		source = 'require '+addslashes(options.file)+';';
	}
	var stream = options.stream || process.stdout;
	return request(source, stream).tap(function() {
		// ensure the stream is flushed before promise is resolved
		return new Promise(function(resolve, reject) {
			stream.write(new Buffer(0), resolve);
		});
	}).nodify(cb);
};

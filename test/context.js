var path = require('path');
var stream = require('readable-stream'); // for node 0.8.x compatability
var util = require('util');

require('should');

var StringStream = function(opts) {
	opts = opts || {};
	StringStream.super_.call(this, opts);
	this._result = '';
	this._encoding = opts.encoding || 'utf8';
};
util.inherits(StringStream, stream.Writable);
StringStream.prototype._write = function(chunk, encoding, callback) {
	this._result += chunk.toString(this._encoding);
	return callback(null);
};
StringStream.prototype.toString = function() {
	return this._result;
};

describe('Passing context object from JS to PHP', function() {
	var php = require('../');
	it('should pass all basic data types from JS to PHP', function() {
		var out = new StringStream();
		return php.request({
			file: path.join(__dirname, 'context.php'),
			stream: out,
			context: {
				a: false,
				b: true,
				c: -42,
				d: (((1<<30)-1)*4),
				e: 1.5,
				f: 'abcdef \uD83D\uDCA9',
				g: { f: 1 },
				h: function fname(x) { return x; },
				i: new Buffer('abc', 'utf8')
			}
		}).then(function(v) {
			out.toString().replace(/int\(4294967292\)/,'float(4294967292)')
			.should.equal(
				'bool(false)\n' +
				'bool(true)\n' +
				'int(-42)\n' +
				'float(4294967292)\n' +
				'float(1.5)\n' +
				'string(11) "abcdef \uD83D\uDCA9"\n' +
				'int(1)\n' +
				'string(5) "fname"\n' +
				'string(3) "abc"\n'
			);
		});
    });
	it('should implement isset(), empty(), and property_exists', function() {
		var out = new StringStream();
		return php.request({
			file: path.join(__dirname, 'context2.php'),
			stream: out,
			context: {
				a: 0,
				b: 42,
				c: null,
				d: undefined,
				e: '0',
				f: '1'
			}
		}).then(function(v) {
			out.toString().should.equal(
				'->a: int(0)\n' +
				'[\'a\']: int(0)\n' +
				'isset: bool(true)\n' +
				'empty: bool(true)\n' +
				'exists: bool(true)\n' +
				'\n' +
				'->b: int(42)\n' +
				'[\'b\']: int(42)\n' +
				'isset: bool(true)\n' +
				'empty: bool(false)\n' +
				'exists: bool(true)\n' +
				'\n' +
				'->c: NULL\n' +
				'[\'c\']: NULL\n' +
				'isset: bool(false)\n' +
				'empty: bool(true)\n' +
				'exists: bool(true)\n' +
				'\n' +
				'->d: NULL\n' +
				'[\'d\']: NULL\n' +
				'isset: bool(false)\n' +
				'empty: bool(true)\n' +
				'exists: bool(true)\n' +
				'\n' +
				'->e: string(1) "0"\n' +
				'[\'e\']: string(1) "0"\n' +
				'isset: bool(true)\n' +
				'empty: bool(true)\n' +
				'exists: bool(true)\n' +
				'\n' +
				'->f: string(1) "1"\n' +
				'[\'f\']: string(1) "1"\n' +
				'isset: bool(true)\n' +
				'empty: bool(false)\n' +
				'exists: bool(true)\n' +
				'\n' +
				'->g: NULL\n' +
				'[\'g\']: NULL\n' +
				'isset: bool(false)\n' +
				'empty: bool(true)\n' +
				'exists: bool(false)\n' +
				'\n'
			);
		});
    });
	it('should handle exceptions in getters', function() {
		var out = new StringStream();
		var context = {};
		Object.defineProperty(context, 'a', { get: function() {
			throw new Error('boo');
		} });
		return php.request({
			source: "call_user_func(function () { try { var_dump($_SERVER['CONTEXT']->a); } catch (Exception $e) { echo 'exception caught'; } })",
			stream: out,
			context: context
		}).then(function() {
			out.toString().should.equal('exception caught');
		});
	});
});

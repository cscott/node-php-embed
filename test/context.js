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

function removeObjectIds(str) {
	return str.replace(/object\(([^\)]*)\)#\d+/g, 'object($1)');
}

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
			removeObjectIds(out.toString())
			.replace(/int\(4294967292\)/,'float(4294967292)')
			.should.equal([
				'bool(false)',
				'bool(true)',
				'int(-42)',
				'float(4294967292)',
				'float(1.5)',
				'string(11) "abcdef \uD83D\uDCA9"',
				'int(1)',
				'string(5) "fname"',
				'int(42)',
				'object(JsBuffer) (1) {',
				'  ["value"]=>',
				'  string(3) "abc"',
				'}',
				''].join('\n')
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
				f: '1',
				g: new Buffer("abc")
			}
		}).then(function(v) {
			removeObjectIds(out.toString()).should.equal([
				'->a: int(0)',
				'[\'a\']: int(0)',
				'isset: bool(true)',
				'empty: bool(true)',
				'exists: bool(true)',
				'',
				'->b: int(42)',
				'[\'b\']: int(42)',
				'isset: bool(true)',
				'empty: bool(false)',
				'exists: bool(true)',
				'',
				'->c: NULL',
				'[\'c\']: NULL',
				'isset: bool(false)',
				'empty: bool(true)',
				'exists: bool(true)',
				'',
				'->d: NULL',
				'[\'d\']: NULL',
				'isset: bool(false)',
				'empty: bool(true)',
				'exists: bool(true)',
				'',
				'->e: string(1) "0"',
				'[\'e\']: string(1) "0"',
				'isset: bool(true)',
				'empty: bool(true)',
				'exists: bool(true)',
				'',
				'->f: string(1) "1"',
				'[\'f\']: string(1) "1"',
				'isset: bool(true)',
				'empty: bool(false)',
				'exists: bool(true)',
				'',
				'->g: object(JsBuffer) (1) {',
				'  ["value"]=>',
				'  string(3) "abc"',
				'}',
				'[\'g\']: object(JsBuffer) (1) {',
				'  ["value"]=>',
				'  string(3) "abc"',
				'}',
				'isset: bool(true)',
				'empty: bool(false)',
				'exists: bool(true)',
				'',
				'->h: NULL',
				'[\'h\']: NULL',
				'isset: bool(false)',
				'empty: bool(true)',
				'exists: bool(false)',
				'',
				''].join('\n')
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
			source: [
				"call_user_func(function () {",
				"  try {",
				"    var_dump($_SERVER['CONTEXT']->a);",
				"  } catch (Exception $e) {",
				"    echo 'exception caught';",
				"  }",
				"})"].join('\n'),
			stream: out,
			context: context
		}).then(function() {
			out.toString().should.equal('exception caught');
		});
	});
	it('should implement __set and __unset', function() {
		var out = new StringStream();
		var context = { a: 42 };
		Object.defineProperty(context, 'b', {
			get: function() { return 13; },
			set: function(v) { this.a = v; }
		});
		return php.request({
			source: [
				"call_user_func(function () {",
				"  $c = $_SERVER['CONTEXT'];",
				"  echo 'a is '; var_dump($c->a);",
				"  echo 'b is '; var_dump($c->b);",
				"  $c->a = 1;",
				"  echo 'a is '; var_dump($c->a);",
				"  echo 'b is '; var_dump($c->b);",
				"  $c->b = 2;",
				"  echo 'a is '; var_dump($c->a);",
				"  echo 'b is '; var_dump($c->b);",
				"  $c['b'] = 3;",
				"  echo 'a is '; var_dump($c->a);",
				"  echo 'b is '; var_dump($c->b);",
				"  unset($c->a);",
				"  echo 'a is '; var_dump($c->a);",
				"  echo 'exists? '; var_dump(property_exists($c, 'a'));",
				"  try {",
				"    unset($c->b);",
				"  } catch (Exception $e) {",
				"    echo 'exception caught';",
				"  }",
				"})"].join('\n'),
			stream: out,
			context: context
		}).then(function() {
			out.toString().should.equal([
				'a is int(42)',
				'b is int(13)',
				'a is int(1)',
				'b is int(13)',
				'a is int(2)',
				'b is int(13)',
				'a is int(3)',
				'b is int(13)',
				'a is NULL',
				'exists? bool(false)',
				'exception caught'
			].join('\n'));
		});
	});
	it('should handle exceptions in setters', function() {
		var out = new StringStream();
		var context = {};
		Object.defineProperty(context, 'a', { set: function() {
			throw new Error('boo');
		} });
		return php.request({
			source: [
				"call_user_func(function () {",
				"  try {",
				"    $_SERVER['CONTEXT']->a = 42;",
				"  } catch (Exception $e) {",
				"    echo 'exception caught';",
				"  }",
				"})"].join('\n'),
			stream: out,
			context: context
		}).then(function() {
			out.toString().should.equal('exception caught');
		});
	});
	it('should allow constructing buffers from PHP', function() {
		var out = new StringStream();
		var context = { b: new Buffer('abc') };
		return php.request({
			source: [
				"call_user_func(function () {",
				"  $b = $_SERVER['CONTEXT']->b;",
				"  $bb = new $b('defgh');",
				"  var_dump($bb);",
				"})"].join('\n'),
			stream: out,
			context: context
		}).then(function() {
			removeObjectIds(out.toString()).should.equal([
				'object(JsBuffer) (1) {',
				'  ["value"]=>',
				'  string(5) "defgh"',
				'}',
				''].join('\n')
			);
		});
	});
	it('should implement __toString', function() {
		var out = new StringStream();
		var A = function A(v) { this.f = v; };
		A.prototype.toString = function() { return JSON.stringify(this.f); };
		var context = { a: new A(32), b: {} };
		return php.request({
			source: [
				"call_user_func(function () {",
				"  $c = $_SERVER['CONTEXT'];",
				"  echo 'a is ' . $c->a . '\n';",
				"  echo 'b is ' . $c->b . '\n';",
				"  $c->a->f = 'abc';",
				"  echo 'a is ' . $c->a . '\n';",
				"})"].join('\n'),
			stream: out,
			context: context
		}).then(function() {
			out.toString().should.equal([
				'a is 32',
				'b is [object Object]',
				'a is "abc"',
				''
			].join('\n'));
		});
	});
});

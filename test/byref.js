// Test cases for passing values "by reference".
var StringStream = require('../test-stream.js');

var path = require('path');
var should = require('should');

describe('Wrapped PHP objects passed by reference to JavaScript', function() {
  var php = require('../');
  var defaultCode = [
    'call_user_func(function () {',
    '  $ctxt = $_SERVER["CONTEXT"];',
    '  #include $ctxt->file;',
    '',
    '  $a = {{VALUE}};',
    '  $b = $a;',
    '  $ctxt->jsfunc(new Js\\ByRef($a));',
    '  var_dump($a);',
    '  var_dump($b);',
    '})',
  ].join('\n');
  var test = function(value, f) {
    var code = defaultCode.replace('{{VALUE}}', value);
    var out = new StringStream();
    return php.request({ source: code, context: {
      jsfunc: f,
      file: path.join(__dirname, 'byref.php'),
    }, stream: out, }).then(function(v) { return [v, out.toString()]; });
  };
  it('arrays', function() {
    return test('array("a"=>"b")',function(a, b) {
      a[1] = 2;
    }).spread(function(v, out) {
      out.should.equal([
        'array(2) {',
        '  ["a"]=>',
        '  string(1) "b"',
        '  [1]=>',
        '  int(2)',
        '}',
        'array(1) {',
        '  ["a"]=>',
        '  string(1) "b"',
        '}',
        '',
      ].join('\n'));
    });
  });
});

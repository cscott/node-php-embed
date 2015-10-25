var StringStream = require('../test-stream.js');
require('should');

describe('Invoking asynchronous JS methods from PHP', function() {
  var php = require('../');
  var context = {
    setTimeout: setTimeout,
    testAsync: function(value1, value2, cb) {
      var value3 = 0;
      if (value1 === 'early') {
        throw new Error('early sync exception');
      }
      process.nextTick(function() {
        if (value1 === 'late') {
          // Throw an exception!
          cb(new Error('late async exception'));
        } else {
          // Normal return case.
          cb(null, value1 + value2 + value3);
        }
      });
      // Demonstrate that execution is asynchronous.
      value3 = 1;
      if (value1 === 'middle') {
        throw new Error('late sync exception (after cb registered)');
      }
    },
  };
  it('should return values and not block the JS thread', function() {
    var out = new StringStream();
    return php.request({
      stream: out,
      context: context,
      source: [
        'call_user_func(function () {',
        '  $setTimeout = $_SERVER["CONTEXT"]->setTimeout;',
        '  $testAsync = $_SERVER["CONTEXT"]->testAsync;',
		// Demonstrate execution of existing node methods
		'  $setTimeout(new Js\\Wait(), 1); # wait 1 ms',
		// Demonstrate that JS thread is not blocked.
		'  return $testAsync(11, 12, new Js\\Wait());',
        '})',
      ].join('\n'),
    }).then(function(v) {
      v.should.equal(24); // Note: not 23
      out.toString().should.equal('');
    });
  });
  it('should handle exceptions thrown sync before callback', function() {
    var out = new StringStream();
    return php.request({
      stream: out,
      context: context,
      source: [
        'call_user_func(function () {',
        '  $testAsync = $_SERVER["CONTEXT"]->testAsync;',
		'  try {',
		'    $testAsync("early", "x", new Js\\Wait());',
		'  } catch (Exception $e) {',
		'    return "exception caught";',
		'  }',
        '})',
      ].join('\n'),
    }).then(function(v) {
      v.should.equal('exception caught');
      out.toString().should.equal('');
    });
  });
  it('should handle exceptions thrown sync after callback', function() {
    var out = new StringStream();
    return php.request({
      stream: out,
      context: context,
      source: [
        'call_user_func(function () {',
        '  $testAsync = $_SERVER["CONTEXT"]->testAsync;',
		'  try {',
		'    $testAsync("middle", "x", new Js\\Wait());',
		'  } catch (Exception $e) {',
		'    return "exception caught";',
		'  }',
        '})',
      ].join('\n'),
    }).then(function(v) {
      v.should.equal('exception caught');
      out.toString().should.equal('');
    });
  });
  it('should handle exceptions returned via callback', function() {
    var out = new StringStream();
    return php.request({
      stream: out,
      context: context,
      source: [
        'call_user_func(function () {',
        '  $testAsync = $_SERVER["CONTEXT"]->testAsync;',
		'  try {',
		'    $testAsync("late", "x", new Js\\Wait());',
		'  } catch (Exception $e) {',
		'    return "exception caught";',
		'  }',
        '})',
      ].join('\n'),
    }).then(function(v) {
      v.should.equal('exception caught');
      out.toString().should.equal('');
    });
  });
});

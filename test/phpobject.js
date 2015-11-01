var StringStream = require('../test-stream.js');

require('should');

describe('Wrapped PHP objects accessed from JavaScript', function() {
  var php = require('../');
  var code = [
    'call_user_func(function () {',
    '  class SimpleClass {',
    '    // property declaration',
    '    public $var = "a default value";',
    '    public $empty = NULL;',
    '    private $priv = "private";',
    '    ',
    '    // method declaration',
    '    public function displayVar() {',
    '      echo $this->var;',
    '    }',
    '    public function getPriv() {',
    '      return $this->priv;',
    '    }',
    '  }',
    '  class MagicClass {',
    '    private $var = "private";',
    '    public function __get( $name ) {',
    '      echo "__get\n";',
    '      return $name == "v" ? $this->var : null;',
    '    }',
    '    public function __set( $name, $value ) {',
    '      echo "__set\n";',
    '      if ( $name == "v" ) { $this->var = $value; }',
    '    }',
    '    public function __isset( $name ) {',
    '      echo "__isset\n";',
    '      return $name == "v";',
    '    }',
    '    public function __unset( $name ) {',
    '      echo "__unset\n";',
    '      if ( $name == "v" ) { $this->var = "unset"; }',
    '    }',
    '    public function __call( $name, $arguments ) {',
    '      echo "__call\n";',
    '      return $name;',
    '    }',
    '    public function __invoke( $x ) {',
    '      echo "__invoke\n";',
    '      return $x;',
    '    }',
    '  }',
    '  $c = $_SERVER["CONTEXT"];',
    '  return $c->jsfunc(new SimpleClass(), new MagicClass());',
    '})',
  ].join('\n');
  var test = function(f) {
    var out = new StringStream();
    return php.request({ source: code, context: { jsfunc: f }, stream: out })
      .then(function(v) { return [v, out.toString()]; });
  };

  describe('should query', function() {
    it('properties', function() {
      return test(function(c) {
        return 'var' in c;
      }).spread(function(v) {
        v.should.equal(true);
      });
    });
    it('properties with $', function() {
      return test(function(c) {
        return '$var' in c;
      }).spread(function(v) {
        v.should.equal(true);
      });
    });
    it('magic properties', function() {
      return test(function(c, m) {
        return '$v' in m;
      }).spread(function(v, out) {
        v.should.equal(true);
        out.should.equal('__isset\n');
      });
    });
    it('private properties', function() {
      return test(function(c) {
        return '$priv' in c;
      }).spread(function(v, out) {
        v.should.equal(false);
      });
    });
    it('private magic properties', function() {
      return test(function(c, m) {
        return '$var' in m;
      }).spread(function(v, out) {
        v.should.equal(false);
        out.should.equal('__isset\n');
      });
    });
    it('consistent with isset()', function() {
      return test(function(c) {
        return '$empty' in c;
      }).spread(function(v) {
        v.should.equal(false);
      });
    });
    it('non-existing properties', function() {
      return test(function(c) {
        return 'foobar' in c;
      }).spread(function(v) {
        v.should.equal(false);
      });
    });
    it('non-existing magic properties', function() {
      return test(function(c, m) {
        return '$x' in m;
      }).spread(function(v, out) {
        v.should.equal(false);
        out.should.equal('__isset\n');
      });
    });
    it('methods', function() {
      return test(function(c) {
        return 'displayVar' in c;
      }).spread(function(v) {
        v.should.equal(true);
      });
    });
    it('non-existing methods (1)', function() {
      return test(function(c) {
        return '$displayVar' in c;
      }).spread(function(v) {
        v.should.equal(false);
      });
    });
    it('non-existing methods (2)', function() {
      return test(function(c) {
        return '__Call' in c;
      }).spread(function(v) {
        v.should.equal(false);
      });
    });
    it('built-in __call method', function() {
      return test(function(c) {
        return '__call' in c;
      }).spread(function(v) {
        v.should.equal(true);
      });
    });
  });
  describe('should get', function() {
    it('properties', function() {
      return test(function(c) {
        return c.var;
      }).spread(function(v) {
        v.should.equal('a default value');
      });
    });
    it('properties with $', function() {
      return test(function(c) {
        return c.$var;
      }).spread(function(v) {
        v.should.equal('a default value');
      });
    });
    it('properties with NULL values', function() {
      return test(function(c) {
        return c.$empty;
      }).spread(function(v) {
        (v === null).should.equal(true);
      });
    });
    it('magic properties', function() {
      return test(function(c, m) {
        return m.$v;
      }).spread(function(v, out) {
        v.should.equal('private');
        out.should.equal('__get\n');
      });
    });
    it('private properties', function() {
      return test(function(c) {
        return c.$priv === undefined;
      }).spread(function(v, out) {
        v.should.equal(true);
      });
    });
    it('private magic properties', function() {
      return test(function(c, m) {
        return m.$var === null;
      }).spread(function(v, out) {
        v.should.equal(true);
        out.should.equal('__get\n');
      });
    });
    it('non-existing properties', function() {
      return test(function(c) {
        return c.foobar === undefined;
      }).spread(function(v) {
        v.should.equal(true);
      });
    });
    it('non-existing magic properties', function() {
      return test(function(c, m) {
        return m.$x === null;
      }).spread(function(v, out) {
        v.should.equal(true);
        out.should.equal('__get\n');
      });
    });
    it('built-in properties (constructor)', function() {
      return test(function(c) {
        return c.constructor === php.PhpObject;
      }).spread(function(v) {
        v.should.equal(true);
      });
    });
    it('methods', function() {
      return test(function(c) {
        return typeof (c.displayVar) === 'function';
      }).spread(function(v) {
        v.should.equal(true);
      });
    });
    it('non-existing methods (1)', function() {
      return test(function(c) {
        return c.$displayVar === undefined;
      }).spread(function(v) {
        v.should.equal(true);
      });
    });
    it('non-existing methods (2)', function() {
      return test(function(c) {
        return c.__Call === undefined;
      }).spread(function(v) {
        v.should.equal(true);
      });
    });
    it('built-in __call method', function() {
      return test(function(c) {
        return typeof (c.__call) === 'function';
      }).spread(function(v) {
        v.should.equal(true);
      });
    });
  });
  describe('should set', function() {
    it('properties', function() {
      return test(function(c) {
        c.var = 'new value';
        return c.var;
      }).spread(function(v) {
        v.should.equal('new value');
      });
    });
    it('properties with $', function() {
      return test(function(c) {
        c.$var = 42;
        return c.$var;
      }).spread(function(v) {
        v.should.equal(42);
      });
    });
    it('properties with NULL values', function() {
      return test(function(c) {
        c.$empty = false;
        return c.$empty;
      }).spread(function(v) {
        (v === false).should.equal(true);
      });
    });
    it('magic properties', function() {
      return test(function(c, m) {
        m.$v = 'magic';
        return m.$v;
      }).spread(function(v, out) {
        v.should.equal('magic');
        out.should.equal('__set\n__get\n');
      });
    });
    it('private properties', function() {
      return test(function(c) {
        c.$priv = 'really?';
        return c.getPriv();
      }).spread(function(v, out) {
        v.should.equal('private');
      });
    });
    it('private magic properties', function() {
      return test(function(c, m) {
        m.$var = 'really?';
        return m.$v;
      }).spread(function(v, out) {
        v.should.equal('private');
        out.should.equal('__set\n__get\n');
      });
    });
    it('non-existing properties', function() {
      return test(function(c) {
        c.foobar = 0.5;
        return c.foobar;
      }).spread(function(v) {
        v.should.equal(0.5);
      });
    });
    it('non-existing magic properties', function() {
      return test(function(c, m) {
        m.$x = 87;
        return m.$x === null;
      }).spread(function(v, out) {
        v.should.equal(true);
        out.should.equal('__set\n__get\n');
      });
    });
    it('built-in properties (constructor; should ignore)', function() {
      return test(function(c) {
        c.constructor = 'ABC';
        // Note: should discard the write.
        return c.constructor === php.PhpObject;
      }).spread(function(v) {
        v.should.equal(true);
      });
    });
    it('methods (should ignore set)', function() {
      return test(function(c) {
        c.displayVar = 'DEF';
        // Note: should discard the write.
        return c.displayVar !== 'DEF';
      }).spread(function(v) {
        v.should.equal(true);
      });
    });
    it('built-in __call method (should ignore set)', function() {
      return test(function(c) {
        c.__call = 'GHI';
        // Note: should discard the write.
        return c.__call !== 'GHI';
      }).spread(function(v) {
        v.should.equal(true);
      });
    });
  });
  describe('should delete', function() {
    it('properties', function() {
      return test(function(c) {
        delete c.var;
        return c.var === undefined && !('var' in c);
      }).spread(function(v) {
        v.should.equal(true);
      });
    });
    it('properties with $', function() {
      return test(function(c) {
        delete c.$var;
        return c.$var === undefined && !('$var' in c);
      }).spread(function(v) {
        v.should.equal(true);
      });
    });
    it('properties with NULL values', function() {
      return test(function(c) {
        delete c.$empty;
        return c.$empty === undefined && !('$empty' in c);
      }).spread(function(v) {
        v.should.equal(true);
      });
    });
    it('magic properties', function() {
      return test(function(c, m) {
        delete m.$v;
        return m.$v;
      }).spread(function(v, out) {
        v.should.equal('unset');
        out.should.equal('__unset\n__get\n');
      });
    });
    it('private properties', function() {
      return test(function(c) {
        delete c.$priv;
        return c.getPriv();
      }).spread(function(v, out) {
        v.should.equal('private');
      });
    });
    it('private magic properties', function() {
      return test(function(c, m) {
        delete m.$var;
        return m.$v;
      }).spread(function(v, out) {
        v.should.equal('private');
        out.should.equal('__unset\n__get\n');
      });
    });
    it('non-existing properties', function() {
      return test(function(c) {
        delete c.foobar;
        return c.foobar === undefined;
      }).spread(function(v) {
        v.should.equal(true);
      });
    });
    it('non-existing magic properties', function() {
      return test(function(c, m) {
        delete m.$x;
        return (m.$x === null) && (m.$v) === 'private';
      }).spread(function(v, out) {
        v.should.equal(true);
        out.should.equal('__unset\n__get\n__get\n');
      });
    });
    it('built-in properties (constructor; should ignore)', function() {
      return test(function(c) {
        delete c.constructor;
        // Note: should discard the write.
        return c.constructor === php.PhpObject;
      }).spread(function(v) {
        v.should.equal(true);
      });
    });
    it('methods (should ignore delete)', function() {
      return test(function(c) {
        delete c.displayVar;
        // Note: should discard the delete.
        return typeof (c.displayVar) === 'function';
      }).spread(function(v) {
        v.should.equal(true);
      });
    });
    it('built-in __call method (should ignore set)', function() {
      return test(function(c) {
        delete c.__call;
        // Note: should discard the delete.
        return typeof (c.__call) === 'function';
      }).spread(function(v) {
        v.should.equal(true);
      });
    });
  });
  describe('should call', function() {
    it('magic methods', function() {
      return test(function(c, m) {
        // Note that `'foo' in m` returns false, so you can't
        // directly invoke `m.foo('bar')`
        return m.__call('foo', 'bar');
      }).spread(function(v) {
        v.should.equal('foo');
      });
    });
  });
  describe('should invoke', function() {
    it.skip('magic methods', function() {
      return test(function(c, m) {
        return m('bar'); // XXX not yet implemented
      }).spread(function(v) {
        v.should.equal('bar');
      });
    });
  });
});

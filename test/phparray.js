var StringStream = require('../test-stream.js');

var should = require('should');

[false, true].forEach(function(useArrayAccess) {
  var title = 'Wrapped PHP ' + (useArrayAccess ? 'ArrayAccess' : 'arrays') +
      ' accessed from JavaScript';
  describe(title, function() {
    var php = require('../');
    var defaultCode = [
      'call_user_func(function () {',
      '  class wrap implements ArrayAccess, Countable {',
      '    private $a;',
      '    public function __construct(&$a) { $this->a =& $a; }',
      '    public function offsetSet($offset, $value) {',
      '      if (is_null($offset)) { $this->a[] = $value; }',
      '      else { $this->a[$offset] = $value; }',
      '    }',
      '    public function offsetExists($offset) {',
      '      return array_key_exists($offset, $this->a);',
      '    }',
      '    public function offsetUnset($offset) {',
      '      unset($this->a[$offset]);',
      '    }',
      '    public function offsetGet($offset) {',
      '      return isset($this->a[$offset]) ? $this->a[$offset] : null;',
      '    }',
      '    public function count() {',
      '      return count($this->a);',
      '    }',
      '  }',
      '  $a = array(1, 2, 3, 4);',
      '  $b = array("foo" => "bar", "bat" => "baz");',
      '  $c = array(1 => 2, "three" => "four");',
      '  $ctxt = $_SERVER["CONTEXT"];',
      useArrayAccess ?
        '  return $ctxt->jsfunc(new wrap($a), new wrap($b), new wrap($c));' :
        '  return $ctxt->jsfunc($a, $b, $c);',
      '})',
    ];
    var test = function(f, code) {
      if (code === undefined) { code = defaultCode; }
      if (Array.isArray(code)) { code = code.join('\n'); }
      var out = new StringStream();
      return php.request({ source: code, context: { jsfunc: f }, stream: out })
        .then(function(v) { return [v, out.toString()]; });
    };

    describe('should query', function() {
      it('length property', function() {
        return test(function(a, b, c) {
          [a,b,c].forEach(function(arr) {
            arr.should.have.propertyWithDescriptor('length', {
              writable: true,
              enumerable: false,
              configurable: false,
            });
            ('length' in arr).should.be.true();
          });
        });
      });
      it('Map interface methods', function() {
        return test(function(a, b, c) {
          [a,b,c].forEach(function(arr) {
            ['get','has','set','keys','size','delete'].forEach(function(meth) {
              arr.should.have.propertyWithDescriptor(meth, {
                writable: false,
                enumerable: false,
                configurable: false,
              });
              (meth in arr).should.be.true();
            });
          });
        });
      });
      it('string properties', function() {
        return test(function(a, b, c) {
          // String properties shouldn't show up directly on the object.
          ('foo' in b).should.be.false();
          ('bat' in b).should.be.false();
          ('three' in c).should.be.false();
        });
      });
      it('string properties via has', function() {
        return test(function(a, b, c) {
          a.has('foo').should.be.false();
          b.has('foo').should.be.true();
          b.has('bat').should.be.true();
          c.has('three').should.be.true();
          c.has('notthere').should.be.false();
        });
      });
      it('indexed properties (numeric)', function() {
        return test(function(a, b, c) {
          (0 in a).should.be.true();
          (1 in a).should.be.true();
          (2 in a).should.be.true();
          (3 in a).should.be.true();
          (4 in a).should.be.false();
          (0 in b).should.be.false();
          (0 in c).should.be.false();
          (1 in c).should.be.true();
        });
      });
      it('indexed properties (string)', function() {
        return test(function(a, b, c) {
          ('0' in a).should.be.true();
          ('1' in a).should.be.true();
          ('2' in a).should.be.true();
          ('3' in a).should.be.true();
          ('4' in a).should.be.false();
          ('0' in b).should.be.false();
          ('0' in c).should.be.false();
          ('1' in c).should.be.true();
        });
      });
      it('indexed properties via has (numeric)', function() {
        return test(function(a, b, c) {
          a.has(0).should.be.true();
          a.has(1).should.be.true();
          a.has(2).should.be.true();
          a.has(3).should.be.true();
          a.has(4).should.be.false();
          b.has(0).should.be.false();
          c.has(0).should.be.false();
          c.has(1).should.be.true();
        });
      });
      it('indexed properties via has (string)', function() {
        return test(function(a, b, c) {
          a.has('0').should.be.true();
          a.has('1').should.be.true();
          a.has('2').should.be.true();
          a.has('3').should.be.true();
          a.has('4').should.be.false();
          b.has('0').should.be.false();
          c.has('0').should.be.false();
          c.has('1').should.be.true();
        });
      });
    });
    describe('should get', function() {
      (useArrayAccess ? it.skip : it)('length property', function() {
        return test(function(a, b, c) {
          a.length.should.equal(4);
          b.length.should.equal(0);
          c.length.should.equal(2);
        });
      });
      it('size via size', function() {
        return test(function(a, b, c) {
          a.size().should.equal(4);
          b.size().should.equal(2);
          c.size().should.equal(2);
        });
      });
      it('Map interface methods', function() {
        return test(function(a, b, c) {
          [a,b,c].forEach(function(arr) {
            ['get','has','set','keys','size','delete'].forEach(function(meth) {
              (typeof arr[meth]).should.equal('function');
            });
          });
        });
      });
      it('string properties', function() {
        return test(function(a, b, c) {
          // String properties shouldn't show up directly on the object.
          should(b.foo).equal(undefined);
          should(b.bat).equal(undefined);
          should(c.three).equal(undefined);
        });
      });
      it('string properties via get', function() {
        return test(function(a, b, c) {
          should(a.get('foo')).equal(undefined);
          b.get('foo').should.equal('bar');
          b.get('bat').should.equal('baz');
          c.get('three').should.equal('four');
          should(c.get('notthere')).equal(undefined);
        });
      });
      it('indexed properties (numeric)', function() {
        return test(function(a, b, c) {
          a[0].should.equal(1);
          a[1].should.equal(2);
          a[2].should.equal(3);
          a[3].should.equal(4);
          should(a[4]).equal(undefined);
          should(b[0]).equal(undefined);
          should(c[0]).equal(undefined);
          c[1].should.equal(2);
        });
      });
      it('indexed properties (string)', function() {
        return test(function(a, b, c) {
          a['0'].should.equal(1);
          a['1'].should.equal(2);
          a['2'].should.equal(3);
          a['3'].should.equal(4);
          should(a['4']).equal(undefined);
          should(b['0']).equal(undefined);
          should(c['0']).equal(undefined);
          c['1'].should.equal(2);
        });
      });
      it('indexed properties via get (numeric)', function() {
        return test(function(a, b, c) {
          a.get(0).should.equal(1);
          a.get(1).should.equal(2);
          a.get(2).should.equal(3);
          a.get(3).should.equal(4);
          should(a.get(4)).equal(undefined);
          should(b.get(0)).equal(undefined);
          should(c.get(0)).equal(undefined);
          c.get(1).should.equal(2);
        });
      });
      it('indexed properties via get (string)', function() {
        return test(function(a, b, c) {
          a.get('0').should.equal(1);
          a.get('1').should.equal(2);
          a.get('2').should.equal(3);
          a.get('3').should.equal(4);
          should(a.get('4')).equal(undefined);
          should(b.get('0')).equal(undefined);
          should(c.get('0')).equal(undefined);
          c.get('1').should.equal(2);
        });
      });
    });
    describe('should set', function() {
      (useArrayAccess ? it.skip : it)('length property', function() {
        return test(function(a, b, c) {
          a.length = 2;
          a.length.should.equal(2);
          a.has(2).should.be.false();
          a.has(3).should.be.false();
          should(a[2]).equal(undefined);
          should(a[3]).equal(undefined);
          a.length = 3;
          a.length.should.equal(3);
          a.has(2).should.be.false();
          should(a[2]).equal(undefined);
          b.length = 1;
          b.length.should.equal(1);
          b.has(0).should.be.false();
          should(b.get(0)).equal(undefined);
          b.has('foo').should.be.true();
          b.has('bat').should.be.true();
          should(b[0]).equal(undefined);
          c.length = 0;
          c.length.should.equal(0);
          c.has(0).should.be.false();
          c.has(1).should.be.false();
          c.has('three').should.be.true();
        });
      });
      it('Map interface methods', function() {
        return test(function(a, b, c) {
          [a,b,c].forEach(function(arr) {
            ['get','has','set','keys','size','delete'].forEach(function(meth) {
              arr[meth] = 42;  // This write should be ignored.
              (typeof arr[meth]).should.equal('function');
            });
          });
        });
      });
      it('string properties', function() {
        return test(function(a, b, c) {
          // String properties shouldn't show up directly on the object.
          a.foo = 'bar';
          a.has('foo').should.be.false();
          b.foo = 'foo';
          b.get('foo').should.equal('bar');
        });
      });
      it('string properties via set', function() {
        return test(function(a, b, c) {
          a.set('foo', 'bar');
          a.has('foo').should.be.true();
          a.get('foo').should.equal('bar');
          // String properties don't affect length.
          (useArrayAccess ? 4 : a.length).should.equal(4);
          a.size().should.equal(5);  // But they do affect size.

          b.set('foo', 'foo');
          b.has('foo').should.be.true();
          b.get('foo').should.equal('foo');
          (useArrayAccess ? 0 : b.length).should.equal(0);
          b.size().should.equal(2);

          // Array keys shouldn't conflict with method names
          var keys = ['set','get','length','keys','size'];
          keys.forEach(function(k) { c.set(k, k); });
          keys.forEach(function(k) { c.get(k).should.equal(k); });
          (useArrayAccess ? 2 : c.length).should.equal(2);
          c.size().should.equal(7);
        });
      });
      it('indexed properties (numeric)', function() {
        return test(function(a, b, c) {
          a[5] = 6;
          a[5].should.equal(6);
          (useArrayAccess ? 6 : a.length).should.equal(6);
          a.size().should.equal(5);
          a.has(4).should.be.false();
          a.has(5).should.be.true();
          a.has(6).should.be.false();

          b[0] = 42;
          b[0].should.equal(42);
          (useArrayAccess ? 1 : b.length).should.equal(1);
          b.size().should.equal(3);
          b.has(0).should.be.true();

          c[0] = 'abc';
          c[0].should.equal('abc');
          (useArrayAccess ? 2 : c.length).should.equal(2);
          c.size().should.equal(3);
        });
      });
      it('indexed properties (string)', function() {
        return test(function(a, b, c) {
          a['5'] = 6;
          a['5'].should.equal(6);
          (useArrayAccess ? 6 : a.length).should.equal(6);
          a.size().should.equal(5);
          a.has(4).should.be.false();
          a.has(5).should.be.true();
          a.has(6).should.be.false();

          b['0'] = 42;
          b['0'].should.equal(42);
          (useArrayAccess ? 1 : b.length).should.equal(1);
          b.size().should.equal(3);
          b.has(0).should.be.true();

          c['0'] = 'abc';
          c['0'].should.equal('abc');
          (useArrayAccess ? 2 : c.length).should.equal(2);
          c.size().should.equal(3);
        });
      });
      it('indexed properties via set (numeric)', function() {
        return test(function(a, b, c) {
          a.set(5, 6);
          a.get(5).should.equal(6);
          (useArrayAccess ? 6 : a.length).should.equal(6);
          a.size().should.equal(5);
          a.has(4).should.be.false();
          a.has(5).should.be.true();
          a.has(6).should.be.false();

          b.set(0, 42);
          b.get(0).should.equal(42);
          (useArrayAccess ? 1 : b.length).should.equal(1);
          b.size().should.equal(3);
          b.has(0).should.be.true();

          c.set(0, 'abc');
          c.get(0).should.equal('abc');
          (useArrayAccess ? 2 : c.length).should.equal(2);
          c.size().should.equal(3);
        });
      });
      it('indexed properties via set (string)', function() {
        return test(function(a, b, c) {
          a.set('5', 6);
          a.get(5).should.equal(6);
          (useArrayAccess ? 6 : a.length).should.equal(6);
          a.size().should.equal(5);
          a.has(4).should.be.false();
          a.has(5).should.be.true();
          a.has(6).should.be.false();

          b.set('0', 42);
          b.get(0).should.equal(42);
          (useArrayAccess ? 1 : b.length).should.equal(1);
          b.size().should.equal(3);
          b.has(0).should.be.true();

          c.set('0', 'abc');
          c.get(0).should.equal('abc');
          (useArrayAccess ? 2 : c.length).should.equal(2);
          c.size().should.equal(3);
        });
      });
    });
    describe('should delete', function() {
      it('length property', function() {
        return test(function(a, b, c) {
          [a,b,c].forEach(function(arr) {
            delete arr.length;  // This delete should be ignored.
            arr.length.should.not.equal(undefined);
          });
        });
      });
      it('Map interface methods', function() {
        return test(function(a, b, c) {
          [a,b,c].forEach(function(arr) {
            ['get','has','set','keys','size','delete'].forEach(function(meth) {
              delete arr[meth];  // This delete should be ignored.
              (typeof arr[meth]).should.equal('function');
            });
          });
        });
      });
      it('string properties', function() {
        return test(function(a, b, c) {
          // String properties shouldn't show up directly on the object.
          delete b.foo;
          b.get('foo').should.equal('bar');
          b.size().should.equal(2);
        });
      });
      it('string properties via delete', function() {
        return test(function(a, b, c) {
          a.delete('foo');
          a.has('foo').should.be.false();
          (useArrayAccess ? 4 : a.length).should.equal(4);
          a.size().should.equal(4);

          b.delete('foo');
          b.has('foo').should.be.false();
          should(b.get('foo')).equal(undefined);
          (useArrayAccess ? 0 : b.length).should.equal(0);
          b.size().should.equal(1);

          c.delete('three');
          c.has('three').should.be.false();
          should(c.get('three')).equal(undefined);
          (useArrayAccess ? 2 : c.length).should.equal(2);
          c.size().should.equal(1);
        });
      });
      it('indexed properties (numeric)', function() {
        return test(function(a, b, c) {
          delete a[3];
          // Note that array is not reindexed:
          (useArrayAccess ? 4 : a.length).should.equal(4);
          a.size().should.equal(3);
          a.has(2).should.be.true();
          a.has(3).should.be.false();
          a.has(4).should.be.false();
          delete a[10];
          (useArrayAccess ? 4 : a.length).should.equal(4);
          a.size().should.equal(3);

          delete b[0];
          b.has(0).should.be.false();
          (useArrayAccess ? 0 : b.length).should.equal(0);
          b.size().should.equal(2);

          delete c[1];
          should(c[0]).equal(undefined);
          should(c[1]).equal(undefined);
          // Note that array is not reindexed:
          (useArrayAccess ? 2 : c.length).should.equal(2);
          c.size().should.equal(1);
        });
      });
      it('indexed properties (string)', function() {
        return test(function(a, b, c) {
          delete a['3'];
          // Note that array is not reindexed:
          (useArrayAccess ? 4 : a.length).should.equal(4);
          a.size().should.equal(3);
          a.has(2).should.be.true();
          a.has(3).should.be.false();
          a.has(4).should.be.false();
          delete a['10'];
          (useArrayAccess ? 4 : a.length).should.equal(4);
          a.size().should.equal(3);

          delete b['0'];
          b.has(0).should.be.false();
          (useArrayAccess ? 0 : b.length).should.equal(0);
          b.size().should.equal(2);

          delete c['1'];
          should(c['0']).equal(undefined);
          should(c['1']).equal(undefined);
          // Note that array is not reindexed:
          (useArrayAccess ? 2 : c.length).should.equal(2);
          c.size().should.equal(1);
        });
      });
      it('indexed properties via delete (numeric)', function() {
        return test(function(a, b, c) {
          a.delete(3);
          // Note that array is not reindexed:
          (useArrayAccess ? 4 : a.length).should.equal(4);
          a.size().should.equal(3);
          a.has(2).should.be.true();
          a.has(3).should.be.false();
          a.has(4).should.be.false();
          a.delete(10);
          (useArrayAccess ? 4 : a.length).should.equal(4);
          a.size().should.equal(3);

          b.delete(0);
          b.has(0).should.be.false();
          (useArrayAccess ? 0 : b.length).should.equal(0);
          b.size().should.equal(2);

          c.delete(1);
          should(c[0]).equal(undefined);
          should(c[1]).equal(undefined);
          // Note that array is not reindexed:
          (useArrayAccess ? 2 : c.length).should.equal(2);
          c.size().should.equal(1);
        });
      });
      it('indexed properties via delete (string)', function() {
        return test(function(a, b, c) {
          a.delete('3');
          // Note that array is not reindexed:
          (useArrayAccess ? 4 : a.length).should.equal(4);
          a.size().should.equal(3);
          a.has(2).should.be.true();
          a.has(3).should.be.false();
          a.has(4).should.be.false();
          a.delete('10');
          (useArrayAccess ? 4 : a.length).should.equal(4);
          a.size().should.equal(3);

          b.delete('0');
          b.has(0).should.be.false();
          (useArrayAccess ? 0 : b.length).should.equal(0);
          b.size().should.equal(2);

          c.delete('1');
          should(c['0']).equal(undefined);
          should(c['1']).equal(undefined);
          // Note that array is not reindexed:
          (useArrayAccess ? 2 : c.length).should.equal(2);
          c.size().should.equal(1);
        });
      });
    });

    it('should be passed by value', function() {
      return test(function(arr) {
        arr.set('foo', 'bar');
      }, [
        'call_user_func(function() {',
        '  $a = array("a" => "b");',
        '  $b = $a;',
        '  $ctxt = $_SERVER["CONTEXT"];',
        '  $ctxt->jsfunc($a);',
        '  var_dump($a);',
        '  var_dump($b);',
        '})',
      ]).spread(function(v, out) {
        // Pass by value; neither a nor b is affected.
        out.should.equal([
          'array(1) {',
          '  ["a"]=>',
          '  string(1) "b"',
          '}',
          'array(1) {',
          '  ["a"]=>',
          '  string(1) "b"',
          '}',
          '',
        ].join('\n'));
      });
    });
    it('can be passed by reference', function() {
      return test(function(arr) {
        arr[1] = 2;
        arr.set('foo', 'bar');
      }, [
        'call_user_func(function() {',
        '  $a = array("a" => "b");',
        '  $b = $a;',
        '  $ctxt = $_SERVER["CONTEXT"];',
        '  $ctxt->jsfunc(new Js\\ByRef($a));',
        '  var_dump($a);',
        '  var_dump($b);',
        '})',
      ]).spread(function(v, out) {
        // Pass by reference; $a is affected but $b is not.
        out.should.equal([
          'array(3) {',
          '  ["a"]=>',
          '  string(1) "b"',
          '  [1]=>',
          '  int(2)',
          '  ["foo"]=>',
          '  string(3) "bar"',
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
});

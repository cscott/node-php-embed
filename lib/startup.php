<?php
// Startup code for PHP request.  This is a good place to implement
// helpers which are easier to write in PHP than in C.
namespace Js;

// Simple marker class to indicate that a given value should be
// passed by reference.  Internally we'll call `getValue` to
// unwrap the reference inside.
class ByRef {
    public $value;
    public function __construct(&$value) { $this->value =& $value; }
    public function &getValue() {
        return $this->value;
    }
}

?>
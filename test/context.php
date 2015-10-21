<?php
$c = $_SERVER['CONTEXT'];
var_dump($c->a);
var_dump($c->b);
var_dump($c->c);
var_dump($c->d);
var_dump($c->e);
var_dump($c->f);
var_dump($c->g->f);
var_dump($c->h->name);
$f = $c->h;
var_dump($f(42));
var_dump($c->i);
?>

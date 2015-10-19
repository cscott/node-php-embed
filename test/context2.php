<?php
$c = $_SERVER['CONTEXT'];

echo "a: "; var_dump($c->a);
echo "isset: "; var_dump(isset($c->a));
echo "empty: "; var_dump(empty($c->a));
echo "exists: "; var_dump(property_exists($c, "a"));
echo "\n";

echo "b: "; var_dump($c->b);
echo "isset: "; var_dump(isset($c->b));
echo "empty: "; var_dump(empty($c->b));
echo "exists: "; var_dump(property_exists($c, "b"));
echo "\n";

echo "c: "; var_dump($c->c);
echo "isset: "; var_dump(isset($c->c));
echo "empty: "; var_dump(empty($c->c));
echo "exists: "; var_dump(property_exists($c, "c"));
echo "\n";

echo "d: "; var_dump($c->d);
echo "isset: "; var_dump(isset($c->d));
echo "empty: "; var_dump(empty($c->d));
echo "exists: "; var_dump(property_exists($c, "d"));
echo "\n";

echo "e: "; var_dump($c->e);
echo "isset: "; var_dump(isset($c->e));
echo "empty: "; var_dump(empty($c->e));
echo "exists: "; var_dump(property_exists($c, "e"));
echo "\n";

echo "f: "; var_dump($c->f);
echo "isset: "; var_dump(isset($c->f));
echo "empty: "; var_dump(empty($c->f));
echo "exists: "; var_dump(property_exists($c, "f"));
echo "\n";

echo "g: "; var_dump($c->g);
echo "isset: "; var_dump(isset($c->g));
echo "empty: "; var_dump(empty($c->g));
echo "exists: "; var_dump(property_exists($c, "g"));
echo "\n";

?>

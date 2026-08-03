--TEST--
Yac data type round-trip for all supported types
--CREDITS--
Jarvis (AI assistant to Laruence)
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
--FILE--
<?php
$yac = new Yac();

/* 1. null */
$yac->set("null_val", null);
var_dump($yac->get("null_val"));

/* 2. bool true */
$yac->set("bool_true", true);
var_dump($yac->get("bool_true"));

/* 3. bool false */
$yac->set("bool_false", false);
var_dump($yac->get("bool_false"));

/* 4. int zero */
$yac->set("int_zero", 0);
var_dump($yac->get("int_zero"));
var_dump($yac->get("int_zero") === 0);

/* 5. int max */
$yac->set("int_max", PHP_INT_MAX);
var_dump($yac->get("int_max"));
var_dump($yac->get("int_max") === PHP_INT_MAX);

/* 6. int negative */
$yac->set("int_neg", -42);
var_dump($yac->get("int_neg"));
var_dump($yac->get("int_neg") === -42);

/* 7. float */
$yac->set("float_val", 3.14159);
var_dump(abs($yac->get("float_val") - 3.14159) < 0.00001);

/* 8. float zero */
$yac->set("float_zero", 0.0);
var_dump($yac->get("float_zero"));
var_dump($yac->get("float_zero") === 0.0);

/* 9. empty string */
$yac->set("empty_str", "");
var_dump($yac->get("empty_str"));

/* 10. simple string */
$yac->set("str", "hello yac");
var_dump($yac->get("str"));

/* 11. flat array with mixed keys */
$flat = [0 => "zero", "a" => "alpha", 1 => 42];
$yac->set("flat_arr", $flat);
$result = $yac->get("flat_arr");
var_dump($result === $flat);

/* 12. nested array */
$nested = [
    "level1" => [
        "level2" => [
            "deep_key" => "deep_value"
        ]
    ],
    "list" => [1, 2, [3, 4]]
];
$yac->set("nested_arr", $nested);
$result = $yac->get("nested_arr");
var_dump($result["level1"]["level2"]["deep_key"]);
var_dump($result["list"][2][1]);

/* 13. stdClass with simple properties */
$obj = new stdClass();
$obj->name = "test";
$obj->count = 42;
$yac->set("simple_obj", $obj);
$result = $yac->get("simple_obj");
var_dump(is_object($result));
var_dump($result->name);
var_dump($result->count);

/* 14. stdClass with nested objects */
$outer = new stdClass();
$outer->id = 1;
$inner = new stdClass();
$inner->value = "nested";
$outer->child = $inner;
$yac->set("nested_obj", $outer);
$result = $yac->get("nested_obj");
var_dump($result->id);
var_dump($result->child->value);

/* 15. large int array (50 elements) */
$big = [];
for ($i = 0; $i < 50; $i++) {
    $big["key_$i"] = $i;
}
$yac->set("big_arr", $big);
$result = $yac->get("big_arr");
var_dump(count($result));
var_dump($result["key_0"]);
var_dump($result["key_49"]);
?>
--EXPECTF--
NULL
bool(true)
bool(false)
int(0)
bool(true)
int(%d)
bool(true)
int(-42)
bool(true)
bool(true)
float(0)
bool(true)
string(0) ""
string(9) "hello yac"
bool(true)
string(10) "deep_value"
int(4)
bool(true)
string(4) "test"
int(42)
int(1)
string(6) "nested"
int(50)
int(0)
int(49)

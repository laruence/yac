--TEST--
Yac constructor prefix edge cases
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--INI--
yac.enable=1
yac.enable_cli=1
yac.keys_memory_size=4M
yac.values_memory_size=32M
--FILE--
<?php
/* 1. empty string prefix — valid */
$yac = new Yac("");
$yac->set("foo", "bar");
var_dump($yac->get("foo"));
$yac = null;

/* 2. no argument — same as empty string */
$yac = new Yac();
$yac->set("foo", "bar2");
var_dump($yac->get("foo"));
$yac = null;

/* 3. prefix + key at exactly YAC_MAX_KEY_LEN (48) — should succeed */
/*    prefix_len=47 + key_len=1 = 48, right at the boundary */
$prefix_len = YAC_MAX_KEY_LEN - 1;             /* 47 */
$prefix47 = str_repeat("a", $prefix_len);
$yac = new Yac($prefix47);
var_dump(strlen($prefix47));                    /* 47 */
$yac->set("k", "ok47");                        /* prefix 47 + 'k' 1 = 48, ok */
var_dump($yac->get("k"));                      /* "ok47" */
$yac = null;

/* 4. prefix exceeds YAC_MAX_KEY_LEN (49 bytes) — should throw */
try {
    $prefix49 = str_repeat("b", YAC_MAX_KEY_LEN + 1);
    $yac = new Yac($prefix49);
    echo "should not reach here\n";
} catch (\Exception $e) {
    var_dump(strpos($e->getMessage(), "48") !== false);
}

/* 5. prefix with special characters — works */
$yac = new Yac("user:");
$yac->set("id", 123);
var_dump($yac->get("id"));
$yac = null;

/* 6. prefix with underscore — works */
$yac = new Yac("app_");
$yac->set("config", "value");
var_dump($yac->get("config"));
$yac = null;

/* 7. prefix with leading/trailing spaces — preserved as-is */
$yac = new Yac(" pre ");
$yac->set("x", "space");
var_dump($yac->get("x"));
$yac = null;

/* 8. cross-check: instances with different prefixes share same memory but different namespaces */
$yac_a = new Yac("a_");
$yac_b = new Yac("b_");
$yac_a->set("k", "from_a");
$yac_b->set("k", "from_b");
var_dump($yac_a->get("k"));
var_dump($yac_b->get("k"));
/* raw access without prefix */
$yac_raw = new Yac();
var_dump($yac_raw->get("a_k"));
var_dump($yac_raw->get("b_k"));
?>
--EXPECTF--
string(3) "bar"
string(4) "bar2"
int(47)
string(4) "ok47"
bool(true)
int(123)
string(5) "value"
string(5) "space"
string(6) "from_a"
string(6) "from_b"
string(6) "from_a"
string(6) "from_b"

--TEST--
Check for yac presence
--SKIPIF--
<?php if (!extension_loaded("yac")) print "skip"; ?>
--FILE--
<?php
echo "yac extension is available\n";
/*
	you can add regression tests for your extension here

  the output of your test code has to be equal to the
  text in the --EXPECT-- section below for the tests
  to pass, differences between the output and the
  expected text are interpreted as failure

	see php5/README.TESTING for further information on
  writing regression tests
*/

/* exposed constants */
var_dump(defined("YAC_VERSION") && is_string(YAC_VERSION) && YAC_VERSION !== "");
var_dump(YAC_MAX_KEY_LEN);
var_dump(YAC_MAX_VALUE_RAW_LEN);
var_dump(YAC_MAX_RAW_COMPRESSED_LEN);
var_dump(defined("YAC_SERIALIZER"));
?>
--EXPECT--
yac extension is available
bool(true)
int(48)
int(67108863)
int(1048576)
bool(true)

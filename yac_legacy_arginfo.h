/* This is a generated file, edit the .stub.php file instead.
 * Stub hash: 94c1b5ff7b5143a852b886d6977a168be8bdc3a3 */

ZEND_STATIC_ASSERT(PHP_VERSION_ID >= 70000, "yac_arginfo.h only supports PHP version ID 70000 or newer, "
	"but it is included on an older PHP version");

ZEND_BEGIN_ARG_INFO_EX(arginfo_class_Yac___construct, 0, 0, 0)
	ZEND_ARG_INFO(0, prefix)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_class_Yac_add, 0, 0, 2)
	ZEND_ARG_INFO(0, key)
	ZEND_ARG_INFO(0, value)
	ZEND_ARG_INFO(0, ttl)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_class_Yac_get, 0, 0, 1)
	ZEND_ARG_INFO(0, key)
	ZEND_ARG_INFO(1, cas)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_class_Yac_set, 0, 0, 1)
	ZEND_ARG_INFO(0, key)
	ZEND_ARG_INFO(0, value)
	ZEND_ARG_INFO(0, ttl)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_class_Yac_delete, 0, 0, 1)
	ZEND_ARG_INFO(0, key)
	ZEND_ARG_INFO(0, delay)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_class_Yac_flush, 0, 0, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_Yac_info arginfo_class_Yac_flush

ZEND_BEGIN_ARG_INFO_EX(arginfo_class_Yac_dump, 0, 0, 0)
	ZEND_ARG_INFO(0, limit)
ZEND_END_ARG_INFO()


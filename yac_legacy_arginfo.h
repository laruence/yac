/* This is a generated file, edit the .stub.php file instead.
 * Stub hash: 82037121dddc7937eca5fe0e8e228b5c414a2116 */

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


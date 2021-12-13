<?php

/** @generate-legacy-arginfo */

class Yac {

	/* properties */
	protected string $_prefix = "";

	/* methods */
	public function __construct(string $prefix = "") {}

	public function add(string|array $key, mixed $value, int $ttl = 0):?bool {}

	public function get(string|array $key, mixed &$cas = NULL):?bool {}

	public function set(string|array $key, mixed $value, int $ttl = 0):?bool {}

	public function delete(string|array $key, int $delay = 0):?bool {}

	public function flush():bool {}

	public function info():array {}

	public function dump(int $limit = 0):?array {}
}

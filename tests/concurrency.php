<?php
/**
 * Yac concurrent read/write hammer.
 *
 * Forks N workers that pound the cache with colliding writes on SHARED
 * keys, strict single-writer PRIVATE keys, and a sea of short-TTL
 * ephemeral keys, then validate every single read.
 *
 * What counts as a WRONG read under concurrency:
 *
 *   - identity-mixup: the value is stamped for another key (slot
 *     identity and value block got out of sync)
 *   - hybrid: header and body disagree — no single write produced this
 *     value
 *   - garbage: unparseable or wrong type (memory corruption)
 *   - stale: a single-writer key returned something other than the
 *     writer's last value (or a miss)
 *
 * What is LEGAL: a miss, or any complete value some worker wrote to
 * that very key. Shared keys may therefore legitimately return any
 * worker's value — attribution is not checked there, only key identity
 * and structural integrity.
 *
 * set() can legitimately fail when the pools are hammered dry (values
 * allocator exhausted, or a slot lock could not be taken) — a failed
 * write leaves the previous state in place. Every write is therefore
 * checked, and the expected-value state is only advanced on success;
 * otherwise the next read of the still-old value would be misjudged as
 * a stale read.
 *
 * Every value is self-describing: it embeds the key name, the writer id
 * and a sequence number, and the body repeats "<key>#<seq>#" units. A
 * wrong read thus carries enough information to identify its origin:
 * the stamped key/writer/seq say who wrote it, and the info() trajectory
 * around the failure says whether kicks/recycles were climbing at the
 * time.
 *
 * Usage (yac must be enabled; the pools must be SMALL so that kicks and
 * value-ring recycles actually happen — wrong values only have a window
 * when the pools churn):
 *
 *   php -n -d extension=modules/yac.so -d yac.enable_cli=1 \
 *       -d yac.keys_memory_size=1M -d yac.values_memory_size=8M \
 *       tests/concurrency.php
 *
 * Tunables (environment): YAC_HAMMER_WORKERS, YAC_HAMMER_OPS,
 * YAC_HAMMER_SEED, YAC_HAMMER_SECONDS. A failure is reproducible in
 * shape (not in scheduling) with the same seed: the per-worker op
 * stream is mt_srand(seed + worker * 7919).
 *
 * YAC_HAMMER_SECONDS > 0 overrides the op budget with a wall-clock
 * deadline — what CI wants, since runner speed varies: the run takes
 * ~N seconds no matter how fast the box is. The shared-key pool is
 * then sized to the worker count to keep contention dense even on
 * small runners.
 *
 * Designed for CI (pcntl required) — exits non-zero on any wrong read.
 */

if (!extension_loaded("pcntl")) {
	fwrite(STDERR, "pcntl is required for the concurrency hammer\n");
	exit(1);
}
if (!extension_loaded("yac")) {
	fwrite(STDERR, "the yac extension is not loaded\n");
	exit(1);
}

try {
	$boot_yac = new Yac();
	$boot_info = $boot_yac->info();
} catch (Throwable $e) {
	$boot_info = null;
}
if (!$boot_info) {
	fwrite(STDERR, "yac is not enabled; run with -d yac.enable_cli=1\n");
	exit(1);
}

function env_int($name, $def) {
	$v = getenv($name);
	return ($v === false || $v === "") ? $def : (int)$v;
}

$workers     = env_int("YAC_HAMMER_WORKERS", 8);
$ops         = env_int("YAC_HAMMER_OPS", 60000);
$seed        = env_int("YAC_HAMMER_SEED", 20260831);
/* seconds > 0 overrides the op budget: run until the deadline, which is
 * what CI wants — a bounded wall-clock regardless of worker speed. the
 * shared keys pool is sized to the number of workers so a small-CI core
 * count still gets full contention */
$seconds     = env_int("YAC_HAMMER_SECONDS", 0);
$shared_keys = $seconds > 0 ? max(16, $workers * 2) : 64;
$deadline    = 0.0;
if ($seconds > 0) {
	$deadline = microtime(true) + $seconds;
	$ops = PHP_INT_MAX; /* the wall-clock deadline ends the run */
}

printf("yac %s on PHP %s: %d workers%s, seed %d\n",
	phpversion("yac"), PHP_VERSION, $workers,
	$seconds > 0 ? " for ~{$seconds}s" : " x {$ops} ops", $seed);
printf("pool: slots_size=%d keys_mem=%d values_mem=%d segment=%dx%d\n",
	$boot_info["slots_size"], $boot_info["slots_memory_size"],
	$boot_info["values_memory_size"], $boot_info["segment_num"],
	$boot_info["segment_size"]);

/* values are self-describing: header stamps key/writer/seq, the body
 * repeats "<key>#<seq>#" units so any hybrid or foreign value fails
 * validation with its origin still attached.
 *
 * values are deliberately LONG: the stored-value integrity check is a
 * full CRC, and a torn read is only plausible if the block is recycled
 * and rewritten mid-read — a big window needs big values. Long values
 * also drag the CRC onto the interleaved hardware path (>4K), so the
 * guard that checks the block here exercises the same code readers use
 * in production. */
function make_value($key, $worker, $seq) {
	$unit = $key . "#" . $seq . "#";
	return "YACCC|k=" . $key . "|w=" . $worker . "|q=" . $seq . "|"
		. str_repeat($unit, mt_rand(300, 2500)) . "|";
}

/* returns null when the value legitimately belongs to $key, otherwise
 * a failure category with detail */
function check_value($key, $v) {
	if (!is_string($v)) {
		return "garbage: expected string, got " . gettype($v);
	}
	if (!preg_match('/^YACCC\|k=([^|]*)\|w=(\d+)\|q=(\d+)\|/', $v, $m)) {
		return "garbage: unparseable header";
	}
	if ($m[1] !== $key) {
		return "identity-mixup: stamped for key '$m[1]' (w$m[2]/q$m[3])";
	}
	$unit = $m[1] . "#" . $m[3] . "#";
	$ulen = strlen($unit);
	$body = substr($v, strlen($m[0]));
	/* how many whole units, then what exactly follows */
	$n = 0;
	while (substr($body, $n * $ulen, $ulen) === $unit) {
		$n++;
	}
	$rest = substr($body, $n * $ulen);
	if ($rest === "|") {
		return null; /* n whole units + trailer: a complete value */
	}
	if ($rest === "") {
		return "truncated: lost the trailer (len=" . strlen($v)
			. ", $n whole units)";
	}
	return "hybrid: $n whole units then foreign tail '"
		. substr($rest, 0, 40) . "' (len=" . strlen($v) . ")";
}

function sample_info($ring, $op, $yac) {
	$ring[] = array($op, $yac->info());
	if (count($ring) > 8) {
		array_shift($ring);
	}
	return $ring;
}

function describe_info_row($row) {
	list($op, $i) = $row;
	return sprintf("op=%d kicks=%d recycles=%d fails=%d slots_used=%d/%d hits=%d miss=%d",
		$op, $i["kicks"], $i["recycles"], $i["fails"],
		$i["slots_used"], $i["slots_size"], $i["hits"], $i["miss"]);
}

function record_failure(&$failures, $worker, $op, $category, $key, $expected, $got, $ring) {
	$raw = is_string($got) ? $got : var_export($got, true);
	$failures[] = array(
		"op"       => $op,
		"category" => $category,
		"key"      => $key,
		"expected" => $expected,
		"got_len"  => strlen($raw),
		"got_head" => substr($raw, 0, 120),
		"got_tail" => substr($raw, -80),
		"ring"     => $ring,
	);
	if (count($failures) <= 5) {
		fwrite(STDERR, sprintf("[w%d] FAIL@op%d %s key=%s\n", $worker, $op, $category, $key));
	}
}

function run_worker($id, $ops, $seed, $shared_keys, $deadline) {
	mt_srand($seed + $id * 7919);
	$yac = new Yac();

	$seq      = 0;      /* shared/ephemeral write sequence */
	$priv_seq = 0;      /* private write sequence */
	$priv_key = "priv_$id";
	$priv_val = null;   /* the exact value the private key must hold */
	$ephemeral = array(); /* rolling window of my recent TTL keys */
	$failures = array();
	$ring     = array();
	$write_fails = 0; /* legitimate set() failures under pool pressure */

	for ($op = 0; $op < $ops; $op++) {
		if (($op & 4095) === 0) {
			$ring = sample_info($ring, $op, $yac);
			/* wall-clock mode: stop once the deadline passes. checked
			 * together with the sampling so the syscall lands at most
			 * once per 4096 ops */
			if ($deadline && microtime(true) >= $deadline) {
				break;
			}
		}
		$r = mt_rand(0, 99);
		if ($r < 25) {
			/* shared set: every worker writes the same key space; 10%
			 * get a TTL so expired slots churn through the free-recycle
			 * path under contention. a failed write keeps the previous
			 * value in place — no worker-local state to roll back here,
			 * but the return is still checked so a systemic write-path
			 * breakage shows up as a counter, not silence */
			$k = "s" . mt_rand(0, $shared_keys - 1);
			$v = make_value($k, $id, ++$seq);
			$ok = (mt_rand(0, 9) === 0)
				? $yac->set($k, $v, 2)
				: $yac->set($k, $v);
			if (!$ok) {
				++$write_fails;
			}
		} elseif ($r < 50) {
			/* shared get: any worker's complete value for this key is
			 * legal; a foreign stamp or a torn body is not */
			$k = "s" . mt_rand(0, $shared_keys - 1);
			$got = $yac->get($k);
			if ($got !== false) {
				$why = check_value($k, $got);
				if ($why !== null) {
					record_failure($failures, $id, $op, $why, $k,
						"any complete value stamped k=$k", $got, $ring);
				}
			}
		} elseif ($r < 70) {
			/* ephemeral set: unique key, short TTL. on a failed write the
			 * seq is rolled back so the worker's stream stays identical */
			$k = "t{$id}_" . (++$seq);
			$v = make_value($k, $id, $seq);
			if ($yac->set($k, $v, mt_rand(1, 2))) {
				$ephemeral[$k] = $v;
				if (count($ephemeral) > 16) {
					array_shift($ephemeral);
				}
			} else {
				--$seq;
				++$write_fails;
			}
		} elseif ($r < 80) {
			/* ephemeral get: only I write this key — a returned value
			 * must be exactly what I wrote (miss is legal: TTL or kick) */
			if ($ephemeral) {
				$k = array_rand($ephemeral);
				$got = $yac->get($k);
				if ($got !== false && $got !== $ephemeral[$k]) {
					record_failure($failures, $id, $op,
						is_string($got) ? check_value($k, $got) : "garbage: type " . gettype($got),
						$k, "my exact last write", $got, $ring);
				}
			}
		} elseif ($r < 90) {
			/* private set: alternates embedded int and block string, so
			 * the same slot flips storage form under writes. a failed
			 * write must leave priv_val untouched: the slot still holds
			 * the previous value, and the next private get would
			 * otherwise be misjudged as a stale read */
			$candidate = ($priv_val === null || is_string($priv_val))
				? mt_rand(1, 1 << 30)
				: make_value($priv_key, $id, $priv_seq + 1);
			if ($yac->set($priv_key, $candidate)) {
				$priv_val = $candidate;
				if (is_string($candidate)) {
					++$priv_seq;
				}
			} else {
				++$write_fails;
			}
		} elseif ($r < 98) {
			/* private get: single writer — must be my last value or a miss */
			if ($priv_val !== null) {
				$got = $yac->get($priv_key);
				if ($got !== false && $got !== $priv_val) {
					record_failure($failures, $id, $op,
						is_string($got) ? check_value($priv_key, $got) : "stale: wrong scalar",
						$priv_key, var_export($priv_val, true), $got, $ring);
				}
			}
		} else {
			/* delete: leaves tombstones that the write path recycles */
			if (mt_rand(0, 1) === 0) {
				$yac->delete("s" . mt_rand(0, $shared_keys - 1));
			} else {
				$yac->delete("t{$id}_" . mt_rand(1, max(1, $seq)));
			}
		}
		if (count($failures) >= 20) {
			break;
		}
	}

	if ($failures) {
		printf("[w%d] %d WRONG READS (seed %d, replay shape with YAC_HAMMER_SEED=%d)\n",
			$id, count($failures), $seed + $id * 7919, $seed);
		foreach ($failures as $f) {
			printf("  op=%d key=%s\n    category: %s\n    expected: %s\n    got_len=%d\n    head: %s\n    tail: %s\n",
				$f["op"], $f["key"], $f["category"], $f["expected"],
				$f["got_len"], $f["got_head"], $f["got_tail"]);
			foreach ($f["ring"] as $row) {
				printf("      %s\n", describe_info_row($row));
			}
		}
		return min(count($failures), 100);
	}
	printf("[w%d] ok: %d ops, seq=%d, write_fails=%d\n", $id, $op, $seq, $write_fails);
	return 0;
}

$boot_yac = new Yac();
$before = $boot_yac->info();
$pids = array();
for ($w = 0; $w < $workers; $w++) {
	$pid = pcntl_fork();
	if ($pid === -1) {
		fwrite(STDERR, "fork failed\n");
		exit(1);
	}
	if ($pid === 0) {
		exit(run_worker($w, $ops, $seed, $shared_keys, $deadline));
	}
	$pids[$w] = $pid;
}

$failed_workers = 0;
foreach ($pids as $w => $pid) {
	pcntl_waitpid($pid, $status);
	$code = pcntl_wifexited($status) ? pcntl_wexitstatus($status) : 255;
	if ($code !== 0) {
		$failed_workers++;
	}
}

$after = $boot_yac->info();
printf("total: hits=%d miss=%d kicks=%d recycles=%d fails=%d slots_used=%d/%d\n",
	$after["hits"] - $before["hits"], $after["miss"] - $before["miss"],
	$after["kicks"] - $before["kicks"], $after["recycles"] - $before["recycles"],
	$after["fails"] - $before["fails"], $after["slots_used"], $after["slots_size"]);
if ($after["kicks"] === $before["kicks"] && $after["recycles"] === $before["recycles"]) {
	printf("WARNING: no kicks and no recycles happened — pools are too big, the hammer did not bite\n");
}

if ($failed_workers > 0) {
	printf("FAIL: %d/%d workers saw wrong reads\n", $failed_workers, $workers);
	exit(1);
}
printf("PASS: every read returned a legitimate value or a miss\n");
exit(0);

#!/bin/sh
# run_mp.sh — run mp_bench.php with explicit extension paths.
#
# php is started with -n (no system ini), so NOTHING is loaded unless a
# .so path is passed here — no ambiguity about which yac/apcu is in use.
# --pcntl is required (mp_bench.php forks workers): pass --pcntl when it
# is built into PHP, or --pcntl=/path/to/pcntl.so otherwise.
#
# Usage:
#   ./run_mp.sh --pcntl[=/path/to/pcntl.so] \
#               [--apcu=/path/to/apcu.so] \
#               [--memcached=/path/to/memcached.so] \
#               [--yac=/path/to/yac.so] \
#               [--mc-host=127.0.0.1] [--mc-port=11211] \
#               [--procs=16 --seconds=5 --ratio=100 --keys=20000] \
#               [--value-size=6]
#
# Which backends run is decided by which extensions get loaded: the apcu
# and memcached backends only run when their .so was passed. yac always
# runs (--yac overrides the default locally built ../modules/yac.so).
set -e
DIR=$(cd "$(dirname "$0")" && pwd)
YAC_SO="$DIR/../modules/yac.so"
APCU_SO=""
MEMC_SO=""
PCNTL_SO=""
PCNTL=0
MC_ARGS=""
PHP_ARGS=""

usage() {
    cat <<'EOF'
Usage:
  ./run_mp.sh --pcntl[=/path/to/pcntl.so] \
              [--apcu=/path/to/apcu.so] \
              [--memcached=/path/to/memcached.so] \
              [--yac=/path/to/yac.so] \
              [--mc-host=127.0.0.1] [--mc-port=11211] \
              [--procs=16 --seconds=5 --ratio=100 --keys=20000] \
              [--value-size=6]

php is started with -n (no system ini), so nothing is loaded unless a
.so path is passed here. --pcntl is required (mp_bench.php forks
workers): pass --pcntl when it is built into PHP, or
--pcntl=/path/to/pcntl.so otherwise.

Which backends run is decided by which extensions get loaded: the apcu
and memcached backends only run when their .so was passed. yac always
runs (--yac overrides the default locally built ../modules/yac.so).
EOF
}

for a in "$@"; do
    case "$a" in
        -h|--help)     usage; exit 0 ;;
        --yac=*)       YAC_SO=${a#--yac=} ;;
        --apcu=*)      APCU_SO=${a#--apcu=} ;;
        --memcached=*) MEMC_SO=${a#--memcached=} ;;
        --pcntl=*)     PCNTL_SO=${a#--pcntl=} ;;
        --pcntl)       PCNTL=1 ;;
        --mc-host=*)   MC_ARGS="$MC_ARGS --host=${a#--mc-host=}" ;;
        --mc-port=*)   MC_ARGS="$MC_ARGS --port=${a#--mc-port=}" ;;
        *)             PHP_ARGS="$PHP_ARGS $a" ;;
    esac
done

php -n -r 'exit(function_exists("pcntl_fork") ? 0 : 1);' || {
	echo "Usage error: --pcntl (or --pcntl=/path/to/pcntl.so) is required." >&2
	exit 1
}

if [ ! -f "$YAC_SO" ]; then
    echo "Cannot find $YAC_SO. Run 'make' in the parent directory first." >&2
    exit 1
fi

# yac     : load the built extension; enlarge key memory so 20k keys fit
#           without eviction; compression stays explicitly off even though
#           these sizes are below the default 4K threshold.
# apc.*   : APCu is off in CLI by default; enable it and size shared memory
#           so the warmed key set fits and hit rate stays at 100%.
exec php -n \
    -d memory_limit=2G \
    ${PCNTL_SO:+-d extension="$PCNTL_SO"} \
    -d extension="$YAC_SO" \
    ${APCU_SO:+-d extension="$APCU_SO"} \
    ${MEMC_SO:+-d extension="$MEMC_SO"} \
    -d yac.enable_cli=1 \
    -d yac.keys_memory_size=32M \
    -d yac.values_memory_size=128M \
    -d yac.compress_threshold=-1 \
    -d apc.enable_cli=1 \
    -d apc.shm_size=160M \
    "$DIR/mp_bench.php" $MC_ARGS $PHP_ARGS

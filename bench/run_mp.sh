#!/bin/sh
# run_mp.sh — load the locally built backends and run mp_bench.php.
#
# Usage:
#   ./run_mp.sh
#   ./run_mp.sh [--procs=16 --seconds=5 --ratio=100 --keys=20000 --vallen=64 --backend=all]
set -e
DIR=$(cd "$(dirname "$0")" && pwd)
YAC_SO="$DIR/../modules/yac.so"

if [ ! -f "$YAC_SO" ]; then
    echo "Cannot find $YAC_SO. Run 'make' in the parent directory first." >&2
    exit 1
fi

# yac     : load the locally built extension; enlarge key memory so 20k keys
#           fit without eviction.
# apc.*   : APCu is off in CLI by default; enable it so the apcu backend runs.
exec php \
    -d extension="$YAC_SO" \
    -d yac.enable_cli=1 \
    -d yac.keys_memory_size=32M \
    -d yac.values_memory_size=128M \
    -d apc.enable_cli=1 \
    "$DIR/mp_bench.php" "$@"

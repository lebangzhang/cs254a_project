#!/bin/bash

THREADS=16
WARPS=8
CONFIGS="-DICACHE_SIZE=65536 -DDCACHE_SIZE=65536"
OUTPUT_FILE="cycles.txt"
> "$OUTPUT_FILE"

run_app() {
    local APP=$1
    local OUTPUT=$2
    echo "$OUTPUT" > "$APP.txt"
    local CYCLES=$(echo "$OUTPUT" | grep -oP 'cycles=\K[0-9]+' | tail -1)
    [ -z "$CYCLES" ] && CYCLES="FAILED"
    echo "$APP,$CYCLES" | tee -a "$OUTPUT_FILE"
}

OUTPUT=$(CONFIGS="$CONFIGS" ./ci/blackbox.sh --driver=simx --threads=$THREADS --warps=$WARPS --app=vecadd --args="-n262144" --perf=1 2>&1)
run_app "vecadd" "$OUTPUT"

OUTPUT=$(CONFIGS="$CONFIGS" ./ci/blackbox.sh --driver=simx --threads=$THREADS --warps=$WARPS --app=pathfinder --args="-n1024" --perf=1 2>&1)
run_app "pathfinder" "$OUTPUT"

OUTPUT=$(CONFIGS="$CONFIGS" ./ci/blackbox.sh --driver=simx --threads=$THREADS --warps=$WARPS --app=dropout_ara_gpu --args="-n262144" --perf=1 2>&1)
run_app "dropout" "$OUTPUT"

OUTPUT=$(CONFIGS="$CONFIGS" ./ci/blackbox.sh --driver=simx --threads=$THREADS --warps=$WARPS --app=dotproduct --args="-n262144" --perf=1 2>&1)
run_app "dotproduct" "$OUTPUT"

OUTPUT=$(CONFIGS="$CONFIGS" ./ci/blackbox.sh --driver=simx --threads=$THREADS --warps=$WARPS --app=bfs --args="-n4096" --perf=1 2>&1)
run_app "bfs" "$OUTPUT"

OUTPUT=$(CONFIGS="$CONFIGS" ./ci/blackbox.sh --driver=simx --threads=$THREADS --warps=$WARPS --app=softmax --args="-n512" --perf=1 2>&1)
run_app "softmax" "$OUTPUT"

OUTPUT=$(CONFIGS="$CONFIGS" ./ci/blackbox.sh --driver=simx --threads=$THREADS --warps=$WARPS --app=sgemm --args="-n128" --perf=1 2>&1) 
run_app "sgemm" "$OUTPUT"

OUTPUT=$(CONFIGS="$CONFIGS" ./ci/blackbox.sh --driver=simx --threads=$THREADS --warps=$WARPS --app=sgemv --args="-n1024" --perf=1 2>&1) 
run_app "sgemv" "$OUTPUT"

OUTPUT=$(CONFIGS="$CONFIGS" ./ci/blackbox.sh --driver=simx --threads=$THREADS --warps=$WARPS --app=jacobi --args="-n256 -t$THREADS" --perf=1 2>&1)
run_app "jacobi" "$OUTPUT"

echo ""
echo "Done! Results saved to $OUTPUT_FILE"

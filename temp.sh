
THREADS=4
WARPS=8
VLEN=4
CONFIGS="-DVLEN=128 -DEXT_V_ENABLE"
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

# OUTPUT=$(CONFIGS="$CONFIGS" ./ci/blackbox.sh --driver=simx --threads=$THREADS --warps=$WARPS --app=vecadd_v --args="-n262144 -v$VLEN" --perf=1 2>&1)
# run_app "vecadd" "$OUTPUT"

# OUTPUT=$(CONFIGS="$CONFIGS" ./ci/blackbox.sh --driver=simx --threads=$THREADS --warps=$WARPS --app=pathfinder_v --args="-n1024 -v$VLEN" --perf=1 2>&1)
# run_app "pathfinder" "$OUTPUT"

# OUTPUT=$(CONFIGS="$CONFIGS" ./ci/blackbox.sh --driver=simx --threads=$THREADS --warps=$WARPS --app=dropout_v --args="-n262144 -v$VLEN" --perf=1 2>&1)
# run_app "dropout" "$OUTPUT"

# OUTPUT=$(CONFIGS="$CONFIGS" ./ci/blackbox.sh --driver=simx --threads=$THREADS --warps=$WARPS --app=dotproduct_v --args="-n262144 -v$VLEN" --perf=1 2>&1)
# run_app "dotproduct" "$OUTPUT"

####################################

# OUTPUT=$(CONFIGS="$CONFIGS" ./ci/blackbox.sh --driver=simx --threads=$THREADS --warps=$WARPS --app=bfs --args="-n4096" --perf=1 2>&1)
# run_app "bfs" "$OUTPUT"

# OUTPUT=$(CONFIGS="$CONFIGS" ./ci/blackbox.sh --threads=4 --warps=8  --app=sgemv --args="-n1024" --perf=1 2>&1)
# run_app "sgemv" "$OUTPUT"

#######################################

# OUTPUT=$(CONFIGS="$CONFIGS" ./ci/blackbox.sh --driver=simx --threads=$THREADS --warps=$WARPS --app=softmax_batch_v --args="-n512" --perf=1 2>&1)
# run_app "softmax" "$OUTPUT"

# OUTPUT=$(CONFIGS="$CONFIGS" ./ci/blackbox.sh --threads=4 --warps=8  --app=sgemm_v --args="-n128" --perf=1 2>&1)
# run_app "sgemm" "$OUTPUT"

# OUTPUT=$(CONFIGS="$CONFIGS" ./ci/blackbox.sh --driver=simx --threads=$THREADS --warps=$WARPS --app=jacobi_v --args="-n256" --perf=1 2>&1)
# run_app "jacobi" "$OUTPUT"


echo ""

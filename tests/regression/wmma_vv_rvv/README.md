# `wmma_vv_rvv` Baseline

This regression is a non-`wmma.vv` baseline for `wmma_vv`.
It uses plain RVV `vfmacc` accumulation and does not require `EXT_TCU_ENABLE`.

It keeps the same:

- host-side input generation
- CPU golden reference
- single-tile `wmma_config_t<NUM_THREADS>` workload
- launch shape (`grid_dim=1`, `block_dim=1`)

and only changes the device-side compute path:

- `wmma_vv`: uses `wmma.vv`
- `wmma_vv_rvv`: uses RVV `vfmacc.vf` outer-product accumulation

Typical comparison flow from `build/`:

```bash
CONFIGS="-DEXT_TCU_ENABLE -DEXT_V_ENABLE -DVLEN=256" \
  ./ci/blackbox.sh --driver=simx --app=wmma_vv --threads=8

CONFIGS="-DEXT_V_ENABLE -DVLEN=256" \
  ./ci/blackbox.sh --driver=simx --app=wmma_vv_rvv --threads=8
```

Compare the final:

```text
PERF: instrs=..., cycles=..., IPC=...
```

Primary metric:

- `cycles`

Useful derived metric:

```text
speedup = rvv_baseline_cycles / wmma_vv_cycles
```

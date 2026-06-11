# `sgemm_tcu_baseline`

Minimal TCU-only baseline for the `wmma_vv` project work.

Properties:

- fixed `NUM_THREADS=8`
- fixed `ITYPE=tf32`, `OTYPE=fp32`
- fixed single-tile `8x8x8` matrix multiply
- uses native TCU `ctx::mma_sync(...)`
- uses the same seeded random stream, quantized to tf32 to match the TCU path used by `wmma_vv`

Examples:

```bash
./run.sh --app sgemm_tcu_baseline --driver simx
./run.sh --app sgemm_tcu_baseline --driver rtlsim
```

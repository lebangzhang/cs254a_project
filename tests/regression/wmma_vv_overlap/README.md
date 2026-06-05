# `wmma_vv_overlap`

This regression complements `wmma_vv` with grouped-register hazard coverage.
It is fixed to the `NUM_THREADS=8` / `8x8x8` tile configuration.

Cases:
- `unique-dst`: disjoint destination group when register space permits
- `dst-eq-srcA`: destination overlaps source A
- `dst-eq-srcB`: destination overlaps source B
- `dst-eq-srcA-twice`: two consecutive `wmma.vv` instructions reuse the A overlap path
- `dst-eq-srcB-twice`: two consecutive `wmma.vv` instructions reuse the B overlap path

Driver behavior:
- `simx`: runs the full overlap suite
- `rtlsim`: runs the full overlap suite

Examples:

```bash
./run.sh --app wmma_vv_overlap --driver=simx
./run.sh --app wmma_vv_overlap --driver=rtlsim
```

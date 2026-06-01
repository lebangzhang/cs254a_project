# `wmma_vv_overlap`

This regression complements `wmma_vv` with grouped-register hazard coverage.

Cases:
- `unique-dst`: disjoint destination group when register space permits
- `dst-eq-srcA`: destination overlaps source A
- `dst-eq-srcB`: destination overlaps source B
- `chain`: second `wmma.vv` consumes the first result directly

Driver behavior:
- `simx`: runs the full overlap suite
- `rtlsim`: runs the full overlap suite

Examples:

```bash
./ci/blackbox.sh --driver=simx --app=wmma_vv_overlap --threads=8
./ci/blackbox.sh --driver=rtlsim --app=wmma_vv_overlap --threads=8
```

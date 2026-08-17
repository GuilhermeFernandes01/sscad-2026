# Frozen analytical package

Every number in the paper is either in this package or derivable from it by a declared transformation.

- `claims.csv`: the paper's 23 claims with their canonical verdicts.
- `analytical-outputs/`: 48 analysis outputs (aggregate sha256 `3b909782...`).
- `normalizadores/`: per-cell means and rank comparison under the three normalizers (N0/N1/N2).
- `provenance.json`: input hashes, analysis-code digest, cell classification lists, unit policy.
- `verify_canonical_results.py`: integrity check; expected rc=0. In this standalone artifact the source-matrix step is skipped with a notice (the source file lives in the research repository); `claims.csv` fidelity is still checked by sha256.

`SHA256SUMS.txt` covers every file here, including this README.

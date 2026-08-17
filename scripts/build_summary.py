#!/usr/bin/env python3
"""build_summary.py: consolidates the per-run summary.json files of a campaign
(e.g. 1760 runs for run-010; frozen binary commit 305e2b2, sha256 36bfe40e...).

Notes on two columns:

  1. `sha256_binario` is read from each run's own accounting file and checked
     against the frozen binary hash, run by run.

  2. `algo_wall_total_us` is kept in the CSV but was measured under load, with
     up to 14 concurrent processes competing for CPU. It is not comparable
     across algorithms; any published computational-cost figure must come from
     an isolated execution.

Procedure:
  1. Scans raw/<cell>/<algorithm>/seed_<N>/summary.json.
  2. Extracts the scalars emitted by the runner, adding cell, algorithm and
     seed (from the path), git_sha (from manifest.json) and sha256_binario
     (from contabilidade.tsv). A missing value stays empty.
  3. Writes processed/fase1_summary_metrics.csv sorted by
     (celula, algoritmo, seed), independent of process completion order.

Usage (from the run directory, with raw/ populated):
  python3 build_summary.py
"""
import csv
import json
import sys
from pathlib import Path

RUN = Path(__file__).resolve().parent.parent
RAW = RUN / "raw"
OUT = RUN / "processed" / "fase1_summary_metrics.csv"

EXPECTED_RUNS = 1760
SHA_CONGELADO = "36bfe40eed33c8b8cb491894acd6f63c05c13e0d7a15a6e6f7b9401246d94c6d"

FIELDS = [
    "total_kwh",
    "total_gco2",
    "total_migrations",
    "total_mig_bytes",
    "mean_active_hosts",
    "final_sla_violations",
    "total_unplaced_vms",
    "algo_wall_total_us",
]


def le_contabilidade(path):
    if not path.exists():
        return {}
    campos = {}
    for linha in path.read_text().splitlines():
        chave, _, valor = linha.partition("\t")
        campos[chave] = valor
    return campos


rows = []
divergentes = []
for summ in sorted(RAW.glob("*/*/seed_*/summary.json")):
    seed_dir = summ.parent.name              # seed_<N>
    algo = summ.parent.parent.name
    celula = summ.parent.parent.parent.name
    seed = int(seed_dir.removeprefix("seed_"))
    data = json.loads(summ.read_text())

    git_sha = ""
    manifest_path = summ.parent / "manifest.json"
    if manifest_path.exists():
        git_sha = json.loads(manifest_path.read_text()).get("git_sha", "")

    cont = le_contabilidade(summ.parent / "contabilidade.tsv")
    sha_bin = cont.get("sha256_binario", "")
    if sha_bin != SHA_CONGELADO:
        divergentes.append(f"{celula}/{algo}/seed_{seed}: {sha_bin or '<missing>'}")

    row = {"celula": celula, "algoritmo": algo, "seed": seed,
           "git_sha": git_sha, "sha256_binario": sha_bin}
    for f in FIELDS:
        row[f] = data.get(f, "")  # a missing field stays empty
    rows.append(row)

rows.sort(key=lambda r: (r["celula"], r["algoritmo"], r["seed"]))

header = ["celula", "algoritmo", "seed", "git_sha", "sha256_binario"] + FIELDS
with OUT.open("w", newline="") as fh:
    w = csv.DictWriter(fh, fieldnames=header)
    w.writeheader()
    w.writerows(rows)

print(f"wrote {OUT} ({len(rows)} runs)")

falhou = False
if len(rows) != EXPECTED_RUNS:
    print(f"WARNING: expected {EXPECTED_RUNS} runs, found {len(rows)}",
          file=sys.stderr)
    falhou = True
if divergentes:
    print(f"WARNING: {len(divergentes)} run(s) outside the frozen binary:",
          file=sys.stderr)
    for d in divergentes[:20]:
        print(f"  {d}", file=sys.stderr)
    falhou = True
sys.exit(1 if falhou else 0)

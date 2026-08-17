#!/usr/bin/env python3
"""build_contabilidade.py: consolidates the per-run accounting of a campaign.

During the campaign no process writes to a shared file: each run writes its
own `contabilidade.tsv` inside its directory. This consolidation runs
afterwards and walks the directories in deterministic order, so the resulting
file does not depend on which process finished first.

Distinction between the recorded times:

  parede_s              wall time of the whole process, measured under load
                        (up to 14 concurrent processes). Campaign cost, not a
                        property of the algorithm.
  cpu_usuario_s + cpu_sistema_s
                        CPU time of the process. Less sensitive to concurrency
                        than wall time, but still includes scenario setup,
                        simulation and persistence.
  algo_wall_total_us    (in summary.json, not here) accumulated decision time
                        of the algorithm, a subset of the above; not the
                        simulation time.

None of the three is interchangeable with the others, and none can be
linearly extrapolated across phases.

Output: processed/contabilidade_fase1.tsv sorted by (celula, algoritmo, seed),
plus a per-cell summary in processed/contabilidade_por_celula.tsv.

Usage (from the run directory, with raw/ populated):
  python3 build_contabilidade.py
"""
import statistics
import sys
from pathlib import Path

RUN = Path(__file__).resolve().parent.parent
RAW = RUN / "raw"
OUT = RUN / "processed" / "contabilidade_fase1.tsv"
OUT_CELULA = RUN / "processed" / "contabilidade_por_celula.tsv"

CAMPOS = ["celula", "algoritmo", "seed", "migracao", "cenario", "inicio", "fim",
          "parede_s", "cpu_usuario_s", "cpu_sistema_s", "pico_memoria_kb",
          "codigo_saida", "tentativas", "trabalhador_pid", "sha256_binario"]


def le(path):
    campos = {}
    for linha in path.read_text().splitlines():
        chave, _, valor = linha.partition("\t")
        campos[chave] = valor
    return campos


linhas = []
for p in sorted(RAW.glob("*/*/seed_*/contabilidade.tsv")):
    c = le(p)
    linhas.append([c.get(k, "") for k in CAMPOS])

# Explicit deterministic order: the glob order is already lexicographic, but
# the seed is numeric and "seed_10" sorts before "seed_9" lexicographically.
linhas.sort(key=lambda r: (r[0], r[1], int(r[2]) if r[2].isdigit() else -1))

with OUT.open("w") as fh:
    fh.write("\t".join(CAMPOS) + "\n")
    for r in linhas:
        fh.write("\t".join(r) + "\n")
print(f"wrote {OUT} ({len(linhas)} runs)")


def num(v):
    try:
        return float(v)
    except (TypeError, ValueError):
        return None


por_celula = {}
for r in linhas:
    d = dict(zip(CAMPOS, r))
    por_celula.setdefault(d["celula"], []).append(d)

with OUT_CELULA.open("w") as fh:
    fh.write("celula\texecucoes\tparede_total_s\tparede_mediana_s\t"
             "parede_max_s\tcpu_total_s\tpico_memoria_max_kb\t"
             "retentativas\tfalhas\n")
    for celula in sorted(por_celula):
        ds = por_celula[celula]
        paredes = [x for x in (num(d["parede_s"]) for d in ds) if x is not None]
        cpus = [x for x in ((num(d["cpu_usuario_s"]) or 0) + (num(d["cpu_sistema_s"]) or 0)
                            for d in ds) if x is not None]
        picos = [x for x in (num(d["pico_memoria_kb"]) for d in ds) if x is not None]
        ret = sum(1 for d in ds if d["tentativas"] not in ("1", ""))
        fal = sum(1 for d in ds if d["codigo_saida"] not in ("0", ""))
        fh.write(f"{celula}\t{len(ds)}\t{sum(paredes):.2f}\t"
                 f"{statistics.median(paredes) if paredes else '':.2f}\t"
                 f"{max(paredes) if paredes else '':.2f}\t{sum(cpus):.2f}\t"
                 f"{max(picos) if picos else ''}\t{ret}\t{fal}\n")
print(f"wrote {OUT_CELULA} ({len(por_celula)} cells)")

falhas = sum(1 for r in linhas if dict(zip(CAMPOS, r))["codigo_saida"] not in ("0", ""))
if falhas:
    print(f"WARNING: {falhas} run(s) with non-zero exit code",
          file=sys.stderr)
    sys.exit(1)

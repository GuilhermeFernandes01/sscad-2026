# Artifact: carbon-aware VM allocation/migration policies (SSCAD 2026)

Simulation code, configurations, derived inputs, analysis pipeline and frozen results for the paper. Simulator: C++20 on SimGrid 4.1.1 (s4u), commit `305e2b2`; campaign binary sha256 `36bfe40e...` (full hashes in `runs/*/run.yaml`).

## Layout

| Path | Contents |
| --- | --- |
| `simulator/` | C++ sources, the 30 canonical scenario YAMLs and the `geo_9dc.yaml` base template (`configs/`) |
| `data/workloads/` | derived traces (arrival, vm_id, cores, MIPS demand) |
| `data/carbon/` | synthetic carbon-intensity series; `em2021/` = how to obtain the Electricity Maps 2021 series (license forbids redistribution) + sha256 of the exact copies |
| `runs/` | per run: `run.yaml` (matrix, seeds, binary), processed outputs, per-file checksums of the raw outputs |
| `analysis/` | analysis scripts, fixed inputs (`inputs/`) and expected outputs (`out/`); `analysis-code-manifest.sha256` pins the exact script versions |
| `canonical-results/` | frozen analytical package backing every number in the paper; self-verifying (see below) |
| `paper-generated/` | deterministic generator for the paper's tables/figures |
| `scripts/` | builders for the per-run `processed/` summaries |
| `MANIFEST.sha256` | sha256 of every file in this artifact |

## Reproducing

Build (requires SimGrid 4.1.1, CMake >= 3.20 and a C++20 compiler; yaml-cpp and nlohmann_json are fetched automatically during configure when not found locally, which needs network access; the pkg-config fallback used when CMake does not find SimGrid directly accepts any SimGrid version, so check that 4.1.1 is the one picked up - if SimGrid is installed in a non-standard prefix, pass `-DCMAKE_PREFIX_PATH=<prefix>` at configure time):

    cmake -S simulator -B build && cmake --build build -j

Run one cell (see `runs/*/run.yaml` for the full campaign matrix - 30 cells x 11 policies x 10 or 20 seeds). Run from the artifact root: the scenario YAMLs reference `data/...` relative to the working directory:

    algosim run simulator/configs/<scenario>.yaml \
        --placement <policy> --migration <policy> --seed <s> --output <dir>

Analyze. The frozen outputs in `analysis/out/` were produced with Python 3.12.3, numpy 2.5.0, pandas 3.0.3 and scipy 1.18.0 (versions read from the analysis venv on the research machine, not recorded in the shipped files); `scipy.stats.wilcoxon` picks exact or approximate p-values by rules that have changed across scipy releases, so other versions may shift p-values in the last decimals. The scripts read the per-seed raw outputs (`runs/*/raw/`, not shipped - see below), so the full chain only runs after re-executing the campaign; from the shipped files alone only `correlacoes_canonicas.py` is runnable. All of them write into `analysis/out/`, overwriting the frozen expected outputs - work on a copy (`reanalise_canonica.py --parcial`, exact token, redirects to `out-parcial/`; `extracao_nivel3.py` has no such flag). Order:

    python3 analysis/reanalise_canonica.py
    python3 analysis/correlacoes_canonicas.py
    # then, in any order: em2021_x_realjun_canonico.py,
    #   equivalencia_estaticos.py, hetcap_canonico.py,
    #   migracao_custo_beneficio_canonico.py, perfil_migracao_canonico.py,
    #   proxy_energia_canonico.py, seeds_10_vs_30_canonico.py,
    #   sobrealocacao_canonica.py
    python3 analysis/extracao_nivel3.py   # last: consumes the others' outputs

The regenerated 48 files must match the shipped `analysis/out/`.

Verify the frozen package and regenerate the paper's tables/figures (the shipped PDFs were produced with matplotlib 3.11.1; another version may not regenerate them byte-identically):

    python3 canonical-results/verify_canonical_results.py   # rc=0
    python3 paper-generated/generate_paper_artifacts.py
    python3 paper-generated/verify_paper_generated.py       # rc=0

## Cited paths

Frozen files (analysis scripts, digests, `claims.csv`, YAML headers) cite some paths of the research repository they were frozen from; those bytes cannot be edited without breaking the hash chain. Mapping:

- `algorithms-simgrid/...` -> this repository's root (e.g. `algorithms-simgrid/src/metrics/sla.cpp` -> `simulator/src/metrics/sla.cpp`)
- `configs/...` (in `runs/*/env/inputs-sha256.txt` and YAML headers) -> `simulator/configs/...`
- `critique-checks/alocacao-identica/out/...` (also cited as `../alocacao-identica/out/...`) -> `analysis/inputs/`
- `runs/<run>/run-manifest.yaml` -> `runs/<run>/run.yaml`
- `../reanalise-canonica/out/...` -> `analysis/out/...` (frozen copies also in `canonical-results/analytical-outputs/`)
- other cited files (`consensus.md`, `experiment-manifest-emenda-desempate.yaml`, reports, scripts and intermediates of earlier runs) stay in the research repository; they are not needed to re-execute or verify anything here.

## Not included

- Raw outputs of the 6,820 runs (~11 GB). Per-file sha256 in `runs/*/checks/SHA256SUMS.txt` allows byte-level verification after re-execution (`sha256sum -c --ignore-missing`).
- Electricity Maps 2021 CSVs (academic license; see `data/carbon/em2021/`).

Derived workloads come from the public Azure 2020 and Google 2011 traces; original trace terms apply.

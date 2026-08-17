#!/usr/bin/env python3
"""Carga única e verificada das campanhas canônicas.

Não analisa nada: decide quais execuções compõem a campanha canônica e sob
quais verificações elas podem ser unidas num único quadro.

Os cálculos do nível 3 partem dos mesmos quatro consolidados e precisam das
mesmas quatro verificações. Um carregador por script criaria cópias que
divergem em silêncio, e divergência aqui não dá erro, dá outro número.

Verificações de integridade (todas abortam, nenhuma avisa e segue):

  I1  consolidado ausente -> saída 2, exceto sob `parcial=True`
  I2  (celula, algoritmo, seed) repetida entre campanhas -> saída 3. Seria a
      mesma execução contada duas vezes, e o teste pareado ficaria com pares
      fabricados.
  I3  conjuntos de sementes distintos entre algoritmos da mesma célula ->
      saída 3. O desenho é balanceado por célula; sem isso o teste pareado
      compararia séries de comprimentos diferentes.
  I4  n_vms divergente entre campanhas para a mesma célula -> saída 3. As
      sementes não pertenceriam à mesma célula e não poderiam ser unidas.

União de sementes: run-010 (42-51) e run-013 (52-71) cobrem as mesmas 16
células da fase 1, com o mesmo binário e cenários de sha256 verificado, e por
isso se unem em 30 sementes por combinação, que é o desenho registrado. O
condutor da run-013 abortaria se algum dos 16 cenários diferisse do da run-010.

As intervenções estruturais (run-011) e os dados históricos (run-012) ficam com
10 sementes. O n é propriedade da célula, não da campanha, e o piso do teste
exato precisa ser declarado célula a célula.

Não é executável: importe `carregar`.
"""
import itertools
import os
import re
import sys

import pandas as pd

HERE = os.path.dirname(os.path.abspath(__file__))
EXP = os.path.normpath(os.path.join(HERE, ".."))
RUNS = os.path.join(EXP, "runs")

# run-id -> arquivo consolidado em processed/. Somente campanhas canônicas.
CANONICAS = [
    ("run-010-fase1-canonica-seeds42-51", "fase1_summary_metrics.csv"),
    ("run-011-fase23-canonica-seeds42-51", "fase23_summary_metrics.csv"),
    ("run-012-em2021-canonica-seeds42-51", "em2021_summary_metrics.csv"),
    ("run-013-fase1-canonica-seeds52-71", "fase1_seeds52_71_summary_metrics.csv"),
]

ALGOS = ["best_fit", "bfd", "cnemesis", "ffd", "first_fit", "follow_renewables",
         "followme_s", "lowest_carbon_dc", "round_robin", "worst_fit", "wsnb"]
MIGR = ["cnemesis", "follow_renewables", "followme_s"]
CA = ["cnemesis", "follow_renewables", "followme_s", "lowest_carbon_dc", "wsnb"]
GB = ["best_fit", "bfd", "ffd", "first_fit", "round_robin", "worst_fit"]
N_PARES = len(list(itertools.combinations(ALGOS, 2)))   # 55, fixo


def n_vms_da_celula(run_id, celula):
    """Lê n_vms do config.yaml congelado da célula, nunca de tabela embutida.

    O número de VMs do trace é propriedade do cenário executado. Lê-lo do
    artefato congelado evita que uma tabela desatualizada no script contamine a
    métrica normalizada.

    A semente não é fixa no caminho porque run-010 e run-013 cobrem as mesmas
    células com sementes disjuntas, e fixar `seed_42` quebraria na run-013. Toma
    a primeira semente em ordem numérica; a escolha não importa, já que `n_vms`
    é idêntico em todas as execuções da célula e a igualdade dos cenários está
    verificada por sha256.
    """
    base = os.path.join(RUNS, run_id, "raw", celula, "lowest_carbon_dc")
    seeds = sorted((d for d in os.listdir(base) if d.startswith("seed_")),
                   key=lambda d: int(d.removeprefix("seed_")))
    if not seeds:
        raise SystemExit(f"nenhuma execução de lowest_carbon_dc em {base}")
    cfg = os.path.join(base, seeds[0], "config.yaml")
    with open(cfg) as fh:
        m = re.search(r"^\s*n_vms:\s*(\d+)", fh.read(), re.M)
    if not m:
        raise SystemExit(f"n_vms não encontrado em {cfg}")
    return int(m.group(1))


class Campanha:
    """Resultado da carga: o quadro unido e os fatos derivados dele.

    Atributos:
      df        execuções, com a coluna `gco2_por_vm` já calculada
      celulas   nomes ordenados
      n_seeds   sementes por célula (propriedade da célula, não da campanha)
      n_vms     VMs por célula, lidas do config.yaml congelado
      ausentes  consolidados que faltaram (só não vazio sob `parcial=True`)
      origens   uma linha por campanha carregada, para o digest
    """

    def __init__(self, df, celulas, n_seeds, n_vms, ausentes, origens):
        self.df = df
        self.celulas = celulas
        self.n_seeds = n_seeds
        self.n_vms = n_vms
        self.ausentes = ausentes
        self.origens = origens


def carregar(parcial, log):
    """Carrega as campanhas canônicas e aplica I1-I4.

    `log` é a função de registro do script chamador. A carga escreve por ela o
    bloco "0. Escopo dos dados", para que todo artefato derivado declare no
    próprio digest sobre quais execuções foi calculado.
    """
    quadros, ausentes, origens = [], [], []
    for run_id, arquivo in CANONICAS:
        caminho = os.path.join(RUNS, run_id, "processed", arquivo)
        if not os.path.isfile(caminho):
            ausentes.append(f"{run_id}/processed/{arquivo}")
            continue
        q = pd.read_csv(caminho)
        q = q.drop(columns=[c for c in q.columns if c == "fonte_carbono"])
        q["run_id"] = run_id
        quadros.append(q)
        origens.append(f"{run_id}: {len(q)} execuções, {q['celula'].nunique()} células")

    # I1
    if ausentes and not parcial:
        print("ABORTA: consolidado canônico ausente — " + "; ".join(ausentes),
              file=sys.stderr)
        print("A reanálise definitiva exige TODAS as campanhas canônicas declaradas. "
              "Use --parcial apenas para desenvolvimento.", file=sys.stderr)
        sys.exit(2)

    df = pd.concat(quadros, ignore_index=True)
    celulas = sorted(df["celula"].unique())

    # I2
    dup = df.duplicated(subset=["celula", "algoritmo", "seed"], keep=False)
    if dup.any():
        amostra = (df[dup][["celula", "algoritmo", "seed", "run_id"]]
                   .head(10).to_string(index=False))
        print("ABORTA: (celula, algoritmo, seed) repetida entre campanhas — a união "
              f"duplicaria execuções.\n{amostra}", file=sys.stderr)
        sys.exit(3)

    # I3
    n_seeds = {}
    for celula in celulas:
        sub = df[df["celula"] == celula]
        conjuntos = {a: frozenset(sub[sub["algoritmo"] == a]["seed"]) for a in ALGOS}
        distintos = set(conjuntos.values())
        if len(distintos) != 1:
            print(f"ABORTA: célula {celula} tem conjuntos de sementes distintos "
                  f"entre algoritmos: "
                  + "; ".join(f"{a}={len(s)}" for a, s in sorted(conjuntos.items())),
                  file=sys.stderr)
            sys.exit(3)
        n_seeds[celula] = len(next(iter(distintos)))

    # I4
    n_vms = {}
    for (run_id, celula), _ in df.groupby(["run_id", "celula"]):
        valor = n_vms_da_celula(run_id, celula)
        anterior = n_vms.setdefault(celula, valor)
        if anterior != valor:
            print(f"ABORTA: célula {celula} tem n_vms divergente entre campanhas "
                  f"({anterior} != {valor}); as sementes não pertencem à mesma "
                  "célula e não podem ser unidas.", file=sys.stderr)
            sys.exit(3)
    df["gco2_por_vm"] = df.apply(
        lambda r: r["total_gco2"] / (n_vms[r["celula"]] - r["total_unplaced_vms"]), axis=1)

    log("== 0. Escopo dos dados ==")
    for o in origens:
        log("  " + o)
    if ausentes:
        log("  AUSENTES (execução --parcial, resultado NÃO citável): " + "; ".join(ausentes))
    log(f"  total: {len(df)} execuções, {len(celulas)} células, {len(ALGOS)} algoritmos")
    log(f"  n_vms por célula, lido do config.yaml congelado: "
        + ", ".join(f"{c}={n_vms[c]}" for c in celulas))

    return Campanha(df, celulas, n_seeds, n_vms, ausentes, origens)

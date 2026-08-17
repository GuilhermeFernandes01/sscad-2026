#!/usr/bin/env python3
"""A-18: perfil intra/inter data center e re-migração.

Fecha a afirmação A-18 do nível 3, o cálculo derivado mais caro: ele não olha
métrica agregada nenhuma, olha o traço de decisões evento a evento.

A afirmação tem quatro partes:

  (i)   `cnemesis` migra 100% dentro do mesmo data center em carga moderada e
        majoritariamente entre data centers em carga alta;
  (ii)  `follow_renewables` migra sempre entre data centers;
  (iii) `followme_s` fica em algum lugar entre os dois, com fração intra que
        varia por célula;
  (iv)  a mesma VM é migrada repetidamente ("ping-pong"), até 14-16 vezes para
        `cnemesis`.

É o cálculo mais exposto ao desempate. Uma sonda no nível de evento mostrou
divergência em 199 de 259 pares de traços entre a campanha antiga e a canônica.
Fração intra/inter e contagem de migrações por VM são funções diretas de qual
hospedeiro foi escolhido, que é o que a política de desempate decide. Nenhum
número desta afirmação pode ser herdado.

Decisões, fixadas antes de ver o resultado:

1. Universo: a campanha canônica inteira, 30 células × 3 algoritmos com
   migração, com o n de cada célula (30 na fase 1, 10 nas demais). O cálculo
   antigo cobria oito células; o artigo canônico reporta o perfil sobre tudo
   o que foi executado.

2. Recorte comparável declarado à parte: as mesmas oito células do cálculo
   antigo, restritas às sementes 42-51. Só nesse recorte a única diferença
   entre as campanhas é a política de desempate, e só nele a comparação
   antigo × novo mede o que promete medir.

3. Execução sem migração não vira zero nem um. `frac_intra` é indefinida
   quando não houve migração, e imputar 0% ou 100% fabricaria perfil. Essas
   execuções são contadas em coluna própria e excluídas do denominador das
   médias, com o denominador impresso ao lado de cada média. O cálculo antigo
   deixava NaN e confiava no descarte silencioso do pandas; aqui o descarte é
   declarado.

4. "100% intra em 60 de 60 sementes" é proposição sobre toda execução, não
   sobre a média. Uma média de 100% é compatível com exceções que se cancelam
   (não neste caso, porque a fração é limitada a 1), mas a proposição continua
   universal e é verificada como universal: conta-se quantas execuções têm
   fração exatamente 1.

5. Regime de carga lido do nome da célula, sufixo `-folgada` (moderada) ou
   `-alta`. O script aborta se alguma célula não terminar em um dos dois: um
   regime inferido por aproximação contaminaria a divisão que a afirmação faz.

6. Sem teste novo. As quatro partes são descrições de perfil, não
   contrastes entre algoritmos. Inventar aqui uma família de testes daria
   aparência inferencial a uma contagem estrutural.

7. A ressalva R-MIG-EMITIDAS vale aqui mais do que em qualquer outro lugar:
   `decisions.jsonl` registra migrações emitidas, não concluídas. "A mesma VM
   foi migrada 14 vezes" é, com rigor, "quatorze decisões de migração foram
   emitidas para a mesma VM".

Uso:
  python3 analysis/perfil_migracao_canonico.py [--parcial]

Saídas em `out/` (ou `out-parcial/`):
  a18_perfil_por_execucao.csv
  a18_perfil_agregado.csv
  a18_recorte_comparavel.csv
  a18_digest.txt
"""
import os
import sys
from collections import Counter

import numpy as np
import pandas as pd

from carga_canonica import MIGR, carregar
from eventos_canonicos import dc_de, eventos

HERE = os.path.dirname(os.path.abspath(__file__))
PARCIAL = "--parcial" in sys.argv
DIR = os.path.join(HERE, "out-parcial" if PARCIAL else "out")
os.makedirs(DIR, exist_ok=True)

SEEDS_ANTIGAS = frozenset(range(42, 52))

# As oito células do cálculo antigo: quatro pares net100/base
CELULAS_ANTIGAS = [
    "net100-az-realjun-folgada", "az-realjun-folgada",
    "net100-go-realjun-folgada", "go-realjun-folgada",
    "net100-az-anticorr-folgada", "az-anticorr-folgada",
    "net100-az-realjun-alta", "az-realjun-alta",
]

linhas = []


def log(msg=""):
    print(msg)
    linhas.append(msg)


def abortar(msg):
    print(f"ABORTADO: {msg}", file=sys.stderr)
    sys.exit(1)


def regime(celula):
    if celula.endswith("-folgada"):
        return "moderada"
    if celula.endswith("-alta"):
        return "alta"
    abortar(f"célula sem sufixo de regime reconhecível: {celula}")


campanha = carregar(PARCIAL, log)
df = campanha.df

log()
log("== A-18. Perfil intra/inter data center das migrações e re-migração ==")
log(f"  universo: {len(campanha.celulas)} células × {len(MIGR)} algoritmos com migração")
log("  eventos lidos de decisions.jsonl, kind == 'migration'; o data center é o")
log("  prefixo do nome do hospedeiro antes de '-h'")
log("  RESSALVA R-MIG-EMITIDAS: são migrações EMITIDAS, não concluídas")

# --- leitura dos eventos, uma linha por execução -----------------------------
reg = []
for celula in campanha.celulas:
    r = regime(celula)
    sementes = sorted(df[df["celula"] == celula]["seed"].unique())
    for alg in MIGR:
        for seed in sementes:
            intra = inter = 0
            por_vm = Counter()
            for ev in eventos(celula, alg, seed):
                if ev["kind"] != "migration":
                    continue
                por_vm[ev["vm_id"]] += 1
                if dc_de(ev["src"]) == dc_de(ev["dst"]):
                    intra += 1
                else:
                    inter += 1
            tot = intra + inter
            reg.append({
                "celula": celula, "regime": r, "algoritmo": alg, "seed": int(seed),
                "migracoes": tot, "intra_dc": intra, "inter_dc": inter,
                "frac_intra": (intra / tot) if tot else np.nan,
                "vms_migradas": len(por_vm),
                "vms_com_mais_de_2_migracoes": sum(1 for v in por_vm.values() if v > 2),
                "max_migracoes_por_vm": max(por_vm.values()) if por_vm else 0,
            })
    print(f"  ... {celula} lida", file=sys.stderr)
exe = pd.DataFrame(reg)

log(f"  execuções lidas: {len(exe)}")
sem_mig = exe[exe["migracoes"] == 0]
detalhe = ", ".join(f"{a}={int((sem_mig['algoritmo'] == a).sum())}" for a in MIGR)
log(f"  execuções sem nenhuma migração: {len(sem_mig)} ({detalhe})")


def agregar(sub):
    """Agrega por (célula, algoritmo) com denominador das médias declarado."""
    saida = []
    for (celula, alg), g in sub.groupby(["celula", "algoritmo"]):
        com = g[g["migracoes"] > 0]
        saida.append({
            "celula": celula, "regime": g["regime"].iloc[0], "algoritmo": alg,
            "execucoes": len(g), "execucoes_sem_migracao": int((g["migracoes"] == 0).sum()),
            "denominador_das_medias": len(com),
            "migracoes_media": float(g["migracoes"].mean()),
            "frac_intra_media": float(com["frac_intra"].mean()) if len(com) else np.nan,
            "frac_intra_min": float(com["frac_intra"].min()) if len(com) else np.nan,
            "frac_intra_max": float(com["frac_intra"].max()) if len(com) else np.nan,
            "execucoes_100pct_intra": int((com["frac_intra"] == 1.0).sum()),
            "execucoes_0pct_intra": int((com["frac_intra"] == 0.0).sum()),
            "max_migracoes_por_vm": int(g["max_migracoes_por_vm"].max()),
            "vms_com_mais_de_2_migracoes_media": float(g["vms_com_mais_de_2_migracoes"].mean()),
        })
    return pd.DataFrame(saida).sort_values(["algoritmo", "celula"])


agg = agregar(exe)

# --- (i) a (iii): perfil por algoritmo e regime ------------------------------
for alg in MIGR:
    sub = agg[agg["algoritmo"] == alg]
    linhas_alg = exe[(exe["algoritmo"] == alg) & (exe["migracoes"] > 0)]
    log()
    log(f"  -- {alg} --")
    for r in ("moderada", "alta"):
        g = linhas_alg[linhas_alg["regime"] == r]
        if len(g) == 0:
            log(f"     carga {r}: nenhuma execução com migração")
            continue
        n100 = int((g["frac_intra"] == 1.0).sum())
        n0 = int((g["frac_intra"] == 0.0).sum())
        log(f"     carga {r}: {len(g)} execuções com migração; "
            f"fração intra média {100*g['frac_intra'].mean():.1f}% "
            f"(mín {100*g['frac_intra'].min():.1f}%, máx {100*g['frac_intra'].max():.1f}%)")
        log(f"       execuções 100% intra: {n100}/{len(g)}; "
            f"execuções 100% inter: {n0}/{len(g)}")
    log(f"     máximo de migrações emitidas para uma mesma VM: "
        f"{int(sub['max_migracoes_por_vm'].max())} "
        f"(célula {sub.loc[sub['max_migracoes_por_vm'].idxmax(), 'celula']})")
    por_celula = sub[sub["denominador_das_medias"] > 0]
    log(f"     por célula, fração intra média: "
        + ", ".join(f"{r['celula']}={100*r['frac_intra_media']:.1f}%"
                    for _, r in por_celula.sort_values("frac_intra_media").iterrows()))

# --- (iv) ping-pong ----------------------------------------------------------
log()
log("  -- (iv) re-migração da mesma VM (migrações EMITIDAS) --")
for alg in MIGR:
    sub = agg[agg["algoritmo"] == alg].sort_values("max_migracoes_por_vm", ascending=False)
    topo = sub.head(3)
    log(f"     {alg}: máximo global {int(sub['max_migracoes_por_vm'].max())}; "
        f"três células de maior máximo: "
        + ", ".join(f"{r['celula']}={int(r['max_migracoes_por_vm'])}"
                    for _, r in topo.iterrows()))

# --- recorte comparável ------------------------------------------------------
faltando = [c for c in CELULAS_ANTIGAS if c not in campanha.celulas]
if faltando:
    abortar(f"células do cálculo antigo ausentes da campanha canônica: {faltando}")
rec = exe[exe["celula"].isin(CELULAS_ANTIGAS) & exe["seed"].isin(SEEDS_ANTIGAS)]
for celula in CELULAS_ANTIGAS:
    for alg in MIGR:
        n = len(rec[(rec["celula"] == celula) & (rec["algoritmo"] == alg)])
        if n != len(SEEDS_ANTIGAS):
            abortar(f"recorte comparável incompleto em {celula}/{alg}: {n} execuções")
agg_rec = agregar(rec)

log()
log("  -- recorte comparável: as 8 células do cálculo antigo, sementes 42-51 --")
log("     (aqui a única diferença entre as campanhas é a política de desempate)")
for alg in MIGR:
    sub = agg_rec[agg_rec["algoritmo"] == alg]
    com = rec[(rec["algoritmo"] == alg) & (rec["migracoes"] > 0)]
    mod = com[com["regime"] == "moderada"]
    alta = com[com["regime"] == "alta"]
    log(f"     {alg}:")
    if len(mod):
        log(f"       carga moderada: {int((mod['frac_intra'] == 1.0).sum())}/{len(mod)} "
            f"execuções 100% intra; fração intra média {100*mod['frac_intra'].mean():.1f}%")
    if len(alta):
        log(f"       carga alta: fração intra média {100*alta['frac_intra'].mean():.1f}% "
            f"(inter {100*(1-alta['frac_intra'].mean()):.1f}%)")
    log(f"       máximo de migrações emitidas por VM: "
        f"{int(sub['max_migracoes_por_vm'].max())}")
    log("       por célula: " + ", ".join(
        f"{r['celula']}={100*r['frac_intra_media']:.1f}%"
        for _, r in sub[sub["denominador_das_medias"] > 0].iterrows()))

exe.to_csv(os.path.join(DIR, "a18_perfil_por_execucao.csv"), index=False)
agg.to_csv(os.path.join(DIR, "a18_perfil_agregado.csv"), index=False)
agg_rec.to_csv(os.path.join(DIR, "a18_recorte_comparavel.csv"), index=False)
log()
log("== artefatos escritos ==")
for nome in ("a18_perfil_por_execucao.csv", "a18_perfil_agregado.csv",
             "a18_recorte_comparavel.csv"):
    log(f"  {nome}")

with open(os.path.join(DIR, "a18_digest.txt"), "w") as fh:
    fh.write("\n".join(linhas) + "\n")
sys.exit(0)

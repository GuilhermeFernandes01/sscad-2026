#!/usr/bin/env python3
"""A-16: o indicador de sobrealocação de CPU.

Fecha a afirmação A-16 do nível 3. São duas proposições em uma frase, e elas
se verificam de formas diferentes:

  (a) o indicador discrimina apenas os algoritmos com migração: os oito
      estáticos nunca sobrealocam;
  (b) a rede restrita não gerou sobrealocação adicional.

O que o indicador mede. `final_sla_violations` é o acumulado, ao longo dos
ticks, de VMs alojadas em hospedeiro cuja soma de `cpu_demand_mips` excede
`cpu_capacity_mips`. É contagem de VM-ticks em hospedeiro sobrecomprometido,
não de violação de contrato de serviço com cliente; o nome herdado é mais forte
do que a grandeza. O artigo já o chama de "indicador de sobrealocação de CPU",
que é a leitura correta, e este script a mantém.

Decisões, fixadas antes de ver o resultado:

1. Proposição (a) sobre a campanha canônica inteira, as 30 células e as
   6.820 execuções, e não só sobre a fase 1. Restringir a população aqui só
   facilitaria encontrar o zero.

2. Denominadores canônicos, declarados. O número antigo cita "1.320/1.320
   linhas da primeira etapa" e "0/320 na rede restrita". Os 320 são
   reproduzíveis por construção (4 células net100 × 8 estáticos × 10 sementes);
   as 1.320 vêm da campanha do artigo anterior, com outra contagem de células,
   e não são recuperáveis daqui. O script imprime os denominadores que a campanha
   canônica realmente tem, sem tentar casá-los com os antigos.

3. Proposição (b) pareada por sementes. As células net100 têm 10 sementes e
   as células-base de fase 1 têm 30. Comparar as duas diretamente mediria também
   o tamanho da amostra; o recorte 42-51 é aplicado ao lado base. Mesma
   disciplina de A-04.

4. "Não gerou sobrealocação adicional" é proposição sobre o máximo, não sobre
   a média. Se qualquer execução sob rede restrita sobrealocar onde a base não
   sobrealocava, a proposição cai; uma média que absorve o caso não a salva.

5. Nenhum limiar novo. O indicador é contagem inteira; a fronteira é zero
   contra não-zero, que já existe no enunciado do artigo.

Aborta se algum par net100/base não existir na campanha, ou se o recorte 42-51
não for exato nas células-base.

Uso:
  python3 analysis/sobrealocacao_canonica.py [--parcial]

Saídas em `out/` (ou `out-parcial/`):
  a16_sobrealocacao_por_algoritmo.csv
  a16_net100_x_base.csv
  a16_digest.txt
"""
import os
import sys

import pandas as pd

from carga_canonica import ALGOS, MIGR, carregar

HERE = os.path.dirname(os.path.abspath(__file__))
PARCIAL = "--parcial" in sys.argv
DIR = os.path.join(HERE, "out-parcial" if PARCIAL else "out")
os.makedirs(DIR, exist_ok=True)

ESTATICOS = [a for a in ALGOS if a not in MIGR]
IND = "final_sla_violations"
SEEDS_PAREADAS = frozenset(range(42, 52))
PARES_REDE = [("net100-az-anticorr-folgada", "az-anticorr-folgada"),
              ("net100-az-realjun-alta", "az-realjun-alta"),
              ("net100-az-realjun-folgada", "az-realjun-folgada"),
              ("net100-go-realjun-folgada", "go-realjun-folgada")]

linhas = []


def log(msg=""):
    print(msg)
    linhas.append(msg)


def abortar(msg):
    print(f"ABORTADO: {msg}", file=sys.stderr)
    sys.exit(1)


campanha = carregar(PARCIAL, log)
df = campanha.df
if IND not in df.columns:
    abortar(f"coluna {IND} ausente das execuções canônicas")

log()
log("== A-16. Indicador de sobrealocação de CPU ==")
log(f"  o indicador é `{IND}`: VMs em hospedeiro cuja demanda somada excede a")
log("  capacidade, acumuladas por tick [origem: algorithms-simgrid/src/metrics/sla.cpp]")

# --- (a) discriminação por algoritmo -----------------------------------------
reg = []
for alg in ALGOS:
    sub = df[df["algoritmo"] == alg]
    nz = sub[sub[IND] > 0]
    reg.append({
        "algoritmo": alg,
        "classe": "com_migracao" if alg in MIGR else "estatico",
        "execucoes": len(sub),
        "execucoes_com_sobrealocacao": len(nz),
        "maximo": int(sub[IND].max()),
        "celulas_com_sobrealocacao": nz["celula"].nunique(),
    })
por_alg = pd.DataFrame(reg)

est = por_alg[por_alg["classe"] == "estatico"]
mig = por_alg[por_alg["classe"] == "com_migracao"]
log()
log(f"  -- (a) discriminação, {len(df)} execuções em {len(campanha.celulas)} células --")
log(f"     estáticos ({len(ESTATICOS)} algoritmos): "
    f"{int(est['execucoes_com_sobrealocacao'].sum())} de "
    f"{int(est['execucoes'].sum())} execuções com indicador > 0")
log(f"     com migração ({len(MIGR)} algoritmos): "
    f"{int(mig['execucoes_com_sobrealocacao'].sum())} de "
    f"{int(mig['execucoes'].sum())} execuções com indicador > 0")
for _, r in por_alg.iterrows():
    log(f"       {r['algoritmo']:18s} {r['classe']:13s} "
        f"{r['execucoes_com_sobrealocacao']:5d}/{r['execucoes']:5d}  "
        f"máximo = {r['maximo']}  células = {r['celulas_com_sobrealocacao']}")
if int(est["execucoes_com_sobrealocacao"].sum()) == 0:
    log("     Os oito estáticos ficam em zero na campanha canônica inteira. A")
    log("     proposição (a) sobrevive, e sobre uma população maior que a antiga.")
else:
    log("     Há estático com indicador > 0: a proposição (a) NÃO sobrevive como")
    log("     enunciada, e o artigo precisa nomear os casos.")

# --- (b) rede restrita contra a base, pareadas por semente -------------------
reg = []
for c_net, c_base in PARES_REDE:
    for c in (c_net, c_base):
        if c not in campanha.celulas:
            abortar(f"célula ausente da campanha canônica: {c}")
    net = df[df["celula"] == c_net]
    base = df[(df["celula"] == c_base) & (df["seed"].isin(SEEDS_PAREADAS))]
    obtidas = frozenset(base["seed"])
    if obtidas != SEEDS_PAREADAS:
        abortar(f"{c_base}: recorte 42-51 incompleto, obtido {sorted(obtidas)}")
    for alg in ALGOS:
        n = net[net["algoritmo"] == alg][IND]
        b = base[base["algoritmo"] == alg][IND]
        reg.append({
            "celula_net100": c_net, "celula_base": c_base, "algoritmo": alg,
            "classe": "com_migracao" if alg in MIGR else "estatico",
            "n_net100": len(n), "n_base_pareado": len(b),
            "maximo_net100": int(n.max()), "maximo_base": int(b.max()),
            "media_net100": float(n.mean()), "media_base": float(b.mean()),
            "nao_zero_net100": int((n > 0).sum()), "nao_zero_base": int((b > 0).sum()),
            "surge_com_rede_restrita": bool(n.max() > 0 and b.max() == 0),
            "piora_com_rede_restrita": bool(n.max() > b.max()),
        })
rede = pd.DataFrame(reg)

surge = rede[rede["surge_com_rede_restrita"]]
piora = rede[rede["piora_com_rede_restrita"]]
log()
log("  -- (b) rede restrita contra a base, sementes 42-51 dos dois lados --")
log(f"     {len(rede)} contrastes (4 pares de células × {len(ALGOS)} algoritmos)")
log(f"     casos em que a sobrealocação SURGE sob rede restrita "
    f"(base = 0, net100 > 0): {len(surge)}")
log(f"     casos em que o máximo PIORA sob rede restrita: {len(piora)}")
for _, r in piora.iterrows():
    log(f"       {r['celula_net100']:28s} {r['algoritmo']:18s} "
        f"base máx {r['maximo_base']} -> net100 máx {r['maximo_net100']}"
        f"{'  [SURGE]' if r['surge_com_rede_restrita'] else ''}")
est_rede = rede[rede["classe"] == "estatico"]
log(f"     estáticos sob rede restrita com indicador > 0: "
    f"{int(est_rede['nao_zero_net100'].sum())} de "
    f"{int(est_rede['n_net100'].sum())} execuções "
    f"(o '0/320' do número antigo tem exatamente este denominador)")
if len(piora) == 0:
    log("     Nenhum contraste piora: a proposição (b) sobrevive na forma forte,")
    log("     que é sobre o máximo e não sobre a média.")
else:
    log("     A proposição (b) precisa ser reescrita: a rede restrita move o")
    log("     indicador nos casos nomeados acima.")

por_alg.to_csv(os.path.join(DIR, "a16_sobrealocacao_por_algoritmo.csv"), index=False)
rede.to_csv(os.path.join(DIR, "a16_net100_x_base.csv"), index=False)
log()
log("== artefatos escritos ==")
for nome in ("a16_sobrealocacao_por_algoritmo.csv", "a16_net100_x_base.csv"):
    log(f"  {nome}")

with open(os.path.join(DIR, "a16_digest.txt"), "w") as fh:
    fh.write("\n".join(linhas) + "\n")
sys.exit(0)

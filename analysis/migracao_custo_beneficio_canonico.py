#!/usr/bin/env python3
"""A-19: vitória simultânea e custo por GiB.

Fecha a afirmação A-19 do nível 3. Duas proposições:

  (a) dos 28 contrastes migrador × configuração, 27 venceram ao mesmo tempo em
      gCO2e e em kWh, com 1 caso dominado declarado;
  (b) a economia ficou entre 0,0086 e 0,2198 gCO2e por GiB migrado.

O cálculo antigo do artigo, anterior à revisão do critério de desempate, usava
Mann-Whitney não pareado; a reanálise canônica usa Wilcoxon pareado. A
proposição (a), porém, não é um teste: é o sinal de duas diferenças de média. O
teste entra aqui só como contexto, e a contagem de destaque continua sendo
sobre sinais, como no artigo.

Decisões, fixadas antes de ver o resultado:

1. Universo declarado: 30 células × 3 migradores = 90 contrastes, contra
   lowest_carbon_dc. O artigo cita 28. O script não tenta chegar a 28:
   reproduzir um total seria fácil e não diria nada.

1b. Recorte comparável, acrescentado depois de verificar a procedência.
   Ao conferir de onde vinham os 28, constatou-se que a campanha antiga tem as
   mesmas 16 células de fase 1 e as mesmas sementes 42-51 da run-010 (1.760
   linhas, 16 células, 11 algoritmos, 10 sementes) e que o cálculo antigo
   rodava sobre doze dessas células, sem as quatro `realjan`.
   Logo existe um recorte em que a única diferença entre a campanha antiga e a
   canônica é a política de desempate: essas doze células, sementes 42-51. Ele
   é computado e reportado à parte, e é ele, não o universo de 90, que permite
   dizer se a afirmação é sensível ao desempate. O acréscimo é de população,
   não de critério: as regras 2 a 5 valem iguais nos dois recortes.

2. Elegibilidade = volume migrado positivo. Um contraste só entra na
   contagem se a média de `total_mig_bytes` do migrador for maior que zero.
   Quem não migrou não tem custo-benefício de migração a avaliar, e incluí-lo
   inflaria a contagem de vitórias com casos vazios. A regra é a mesma do
   cálculo antigo, que devolvia NaN quando o volume era zero.

3. Vitória = as duas diferenças de média negativas. Diferença exatamente
   nula em qualquer das duas métricas é empate, categoria própria, nunca
   vitória. "Dominado" é o caso do artigo: pior em pelo menos uma das duas.

4. gCO2e por GiB só onde há economia e volume. A razão −Δ/GiB é reportada
   apenas para os contrastes que vencem em gCO2e e migram; nos demais ela não
   tem sentido de "economia por GiB" e fica em branco em vez de virar número
   negativo apresentável.

5. Unidade declarada, não deduzida no meio da conta. `total_mig_bytes` está
   em MiB, e a conversão para GiB é divisão por 1024, a mesma do cálculo
   antigo. A ressalva R-MIGBYTES-V2 registra que a coluna conta bytes de fato
   transferidos, e que `total_migrations` conta migrações emitidas, não
   concluídas.

6. Verificação do efeito de n. As 16 células de fase 1 têm 30 sementes e as
   demais têm 10. A classificação é refeita nas de 30 restrita às sementes
   42-51, e o script reporta quantos contrastes mudam de categoria. Sinal de
   média é sensível a amostra; se algum mudar, a contagem de destaque não é
   comparável com a antiga sem essa nota.

Aborta se faltar coluna, se algum recorte 42-51 for incompleto ou se alguma
célula não tiver os 11 algoritmos.

Uso:
  python3 analysis/migracao_custo_beneficio_canonico.py [--parcial]

Saídas em `out/` (ou `out-parcial/`):
  a19_custo_beneficio.csv
  a19_digest.txt
"""
import os
import sys

import numpy as np
import pandas as pd

from carga_canonica import ALGOS, MIGR, carregar
from estatistica_comum import (PAR_ESTRUTURAL, celulas_degeneradas, holm,
                               wsr_p)

HERE = os.path.dirname(os.path.abspath(__file__))
EXP = os.path.normpath(os.path.join(HERE, ".."))
PARCIAL = "--parcial" in sys.argv
DIR = os.path.join(HERE, "out-parcial" if PARCIAL else "out")
os.makedirs(DIR, exist_ok=True)

BASE = "lowest_carbon_dc"
MIB_POR_GIB = 1024.0
SEEDS_PAREADAS = frozenset(range(42, 52))

# As doze células do cálculo antigo do artigo. A fase 1 tem dezesseis; as
# quatro `realjan` ficaram de fora porque são bit a bit idênticas às `realjun`
# correspondentes, daí a matriz efetiva de 12 células.
CELULAS_ANTIGAS = [
    "az-realjun-folgada", "az-realjun-alta",
    "az-flat-folgada", "az-flat-alta",
    "az-anticorr-folgada", "az-anticorr-alta",
    "go-realjun-folgada", "go-realjun-alta",
    "go-flat-folgada", "go-flat-alta",
    "go-anticorr-folgada", "go-anticorr-alta",
]

linhas = []


def log(msg=""):
    print(msg)
    linhas.append(msg)


def abortar(msg):
    print(f"ABORTADO: {msg}", file=sys.stderr)
    sys.exit(1)


campanha = carregar(PARCIAL, log)
df = campanha.df
for col in ("total_gco2", "total_kwh", "total_mig_bytes"):
    if col not in df.columns:
        abortar(f"coluna {col} ausente das execuções canônicas")
estruturais, crit = celulas_degeneradas(EXP)

log()
log("== A-19. Vitória simultânea em gCO2e e kWh, e custo por GiB migrado ==")
log(f"  universo declarado: {len(campanha.celulas)} células × {len(MIGR)} "
    f"migradores = {len(campanha.celulas) * len(MIGR)} contrastes contra {BASE}")
log("  `total_mig_bytes` está em MiB; GiB = MiB / 1024 (R-MIGBYTES-V2: bytes")
log("  realmente transferidos; `total_migrations` conta migrações EMITIDAS)")


def classificar(celula, sementes=None):
    """Devolve os três contrastes da célula, já classificados.

    A família de Holm é a primária (m = 3) e é montada inteira antes de
    qualquer filtro de elegibilidade: filtrar antes reduziria a família com
    base nos dados, que é o que RE-2 proíbe.
    """
    sub = df[df["celula"] == celula]
    if sementes is not None:
        sub = sub[sub["seed"].isin(sementes)]
        if frozenset(sub["seed"]) != sementes:
            abortar(f"{celula}: recorte 42-51 incompleto")
    if set(sub["algoritmo"]) != set(ALGOS):
        abortar(f"{celula}: algoritmos != os 11 canônicos")
    b = sub[sub["algoritmo"] == BASE].sort_values("seed")
    bg = b["total_gco2"].to_numpy()
    bk = b["total_kwh"].to_numpy()
    fam = []
    for alg in MIGR:
        x = sub[sub["algoritmo"] == alg].sort_values("seed")
        dg = x["total_gco2"].to_numpy() - bg
        dk = x["total_kwh"].to_numpy() - bk
        gib = float(x["total_mig_bytes"].mean()) / MIB_POR_GIB
        mg, mk = float(dg.mean()), float(dk.mean())
        deg = celula in estruturais and tuple(sorted((alg, BASE))) == PAR_ESTRUTURAL
        if gib <= 0:
            cat = "nao_elegivel_sem_migracao"
        elif mg == 0 or mk == 0:
            cat = "empate"
        elif mg < 0 and mk < 0:
            cat = "vence_ambos"
        else:
            cat = "dominado"
        fam.append({
            "celula": celula, "algoritmo": alg, "n_seeds": len(dg),
            "categoria": cat,
            "dif_gco2": mg, "dif_gco2_pct": 100 * mg / bg.mean(),
            "dif_kwh": mk, "dif_kwh_pct": 100 * mk / bk.mean(),
            "gib_migrados": gib,
            "gco2_por_gib": (-mg / gib) if (gib > 0 and mg < 0) else np.nan,
            "degenerado_por_construcao": deg,
            "p_bruto": 1.0 if deg else float(wsr_p(dg)),
        })
    for r, aj in zip(fam, holm([r["p_bruto"] for r in fam])):
        r["p_holm3"] = float(aj)
        r["significativo_gco2"] = bool(aj < 0.05)
    return fam


registros = []
for celula in campanha.celulas:
    registros.extend(classificar(celula))
cb = pd.DataFrame(registros)

eleg = cb[cb["categoria"] != "nao_elegivel_sem_migracao"]
vence = eleg[eleg["categoria"] == "vence_ambos"]
dominado = eleg[eleg["categoria"] == "dominado"]
empate = eleg[eleg["categoria"] == "empate"]

log()
log(f"  -- (a) vitória simultânea --")
log(f"     contrastes elegíveis (volume migrado > 0): {len(eleg)} de {len(cb)}")
log(f"     vencem em gCO2e E em kWh: {len(vence)}/{len(eleg)}")
log(f"     dominados (piores em pelo menos uma): {len(dominado)}/{len(eleg)}")
log(f"     empates (diferença exatamente nula em uma das duas): {len(empate)}/{len(eleg)}")
for _, r in dominado.iterrows():
    log(f"       DOMINADO  {r['celula']:28s} {r['algoritmo']:18s} "
        f"gCO2e {r['dif_gco2_pct']:+.3f}%  kWh {r['dif_kwh_pct']:+.3f}%  "
        f"p_holm3 = {r['p_holm3']:.4g}")
for _, r in empate.iterrows():
    log(f"       EMPATE    {r['celula']:28s} {r['algoritmo']:18s} "
        f"gCO2e {r['dif_gco2_pct']:+.3f}%  kWh {r['dif_kwh_pct']:+.3f}%")
nao_eleg = cb[cb["categoria"] == "nao_elegivel_sem_migracao"]
log(f"     fora da contagem por não migrarem: {len(nao_eleg)} contrastes, em "
    f"{nao_eleg['celula'].nunique()} células — majoritariamente "
    f"{nao_eleg['algoritmo'].value_counts().idxmax()}")
log(f"     dos {len(vence)} que vencem, {int(vence['significativo_gco2'].sum())} "
    f"têm o gCO2e significativo sob Holm m = 3 (contexto; a contagem de")
log("     destaque é sobre sinal de média, como no artigo)")

log()
log("  -- (b) gCO2e economizados por GiB migrado --")
razao = vence["gco2_por_gib"].dropna()
log(f"     {len(razao)} contrastes com razão definida: "
    f"mínimo {razao.min():.4f}, máximo {razao.max():.4f}, "
    f"mediana {razao.median():.4f} gCO2e/GiB")
for _, r in vence.nsmallest(3, "gco2_por_gib").iterrows():
    log(f"       menor  {r['celula']:28s} {r['algoritmo']:18s} "
        f"{r['gco2_por_gib']:.4f} gCO2e/GiB ({r['gib_migrados']:.1f} GiB)")
for _, r in vence.nlargest(3, "gco2_por_gib").iterrows():
    log(f"       maior  {r['celula']:28s} {r['algoritmo']:18s} "
        f"{r['gco2_por_gib']:.4f} gCO2e/GiB ({r['gib_migrados']:.1f} GiB)")

# --- (6) efeito do tamanho de amostra ----------------------------------------
de30 = [c for c in campanha.celulas if campanha.n_seeds[c] == 30]
mudou = []
for celula in de30:
    for novo, antigo in zip(classificar(celula, SEEDS_PAREADAS),
                            [r for r in registros if r["celula"] == celula]):
        if novo["categoria"] != antigo["categoria"]:
            mudou.append((celula, novo["algoritmo"], antigo["categoria"],
                          novo["categoria"]))
log()
log(f"  -- efeito do tamanho de amostra, {len(de30)} células de 30 sementes --")
log(f"     contrastes que mudam de categoria ao restringir às sementes 42-51: "
    f"{len(mudou)}")
for celula, alg, a, b in mudou:
    log(f"       {celula:28s} {alg:18s} {a} -> {b}")
if not mudou:
    log("     Nenhum. A contagem de destaque não depende de a fase 1 ter 30")
    log("     sementes e o restante da campanha ter 10.")

# --- (1b) recorte comparável ao número antigo --------------------------------
faltando = [c for c in CELULAS_ANTIGAS if c not in campanha.celulas]
if faltando:
    abortar(f"células do recorte comparável ausentes: {faltando}")
comp = []
for celula in CELULAS_ANTIGAS:
    comp.extend(classificar(celula, SEEDS_PAREADAS))
comp = pd.DataFrame(comp)
comp["recorte"] = "doze_celulas_seeds42_51"
c_eleg = comp[comp["categoria"] != "nao_elegivel_sem_migracao"]
c_vence = c_eleg[c_eleg["categoria"] == "vence_ambos"]
c_razao = c_vence["gco2_por_gib"].dropna()
log()
log("  -- recorte comparável ao número antigo: 12 células, sementes 42-51 --")
log("     mesma lista de células e mesmas sementes do cálculo antigo; a única")
log("     diferença em relação a ele é a política de desempate")
log(f"     elegíveis: {len(c_eleg)} de {len(comp)}   "
    f"(o número antigo declarava 28 de 36)")
log(f"     vencem em gCO2e E em kWh: {len(c_vence)}/{len(c_eleg)}   "
    f"(o número antigo declarava 27/28, 1 dominado)")
for _, r in c_eleg[c_eleg["categoria"] == "dominado"].iterrows():
    log(f"       DOMINADO  {r['celula']:28s} {r['algoritmo']:18s} "
        f"gCO2e {r['dif_gco2_pct']:+.3f}%  kWh {r['dif_kwh_pct']:+.3f}%")
log(f"     gCO2e por GiB: mínimo {c_razao.min():.4f}, máximo {c_razao.max():.4f} "
    f"(o número antigo declarava 0,0086 a 0,2198)")
if len(c_eleg) != 28:
    log(f"     A diferença de elegibilidade ({len(c_eleg)} contra 28) é resultado,")
    log("     não erro de recorte: sob o binário canônico há contrastes que passam")
    log("     a migrar onde antes o volume era zero, ou o inverso. O artigo não")
    log("     pode citar '27 de 28' sem dizer que o denominador mudou.")

cb["recorte"] = "campanha_completa"
pd.concat([cb, comp], ignore_index=True).to_csv(
    os.path.join(DIR, "a19_custo_beneficio.csv"), index=False)
log()
log("== artefatos escritos ==")
log("  a19_custo_beneficio.csv")

with open(os.path.join(DIR, "a19_digest.txt"), "w") as fh:
    fh.write("\n".join(linhas) + "\n")
sys.exit(0)

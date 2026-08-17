#!/usr/bin/env python3
"""A-05: o ranking muda ao passar de 10 para 30 sementes?

Fecha a afirmação A-05 do nível 3. O cálculo antigo do artigo é anterior à
revisão do critério de desempate, e por isso nenhum número dele é citável.

Mudança de população, declarada antes de qualquer resultado. Esta não é a mesma
pergunta empírica do artigo antigo:

  - O cálculo antigo testava duas células de fase 2-3 escolhidas por terem
    coeficiente de variação alto: `net100-go-realjun-folgada` e
    `pue-go-realjun-folgada`. Era um recorte adversarial, as células onde a
    instabilidade era mais provável.
  - Na campanha canônica, as trinta sementes existem apenas nas dezesseis
    células da fase 1 (run-010 ⊎ run-013). As intervenções estruturais
    (run-011) e os dados históricos (run-012) ficaram com dez sementes, por
    decisão de desenho.

Logo, as duas células do teste antigo não são testáveis a trinta sementes na
campanha canônica, e a resposta canônica cobre outro conjunto de células. O
script verifica e registra isso, em vez de deixar a troca passar como
continuidade. A consequência para o nível 3 é que A-05 não pode ser
"Preservada": no melhor caso é "Reformulada", porque a população mudou.

Decisões estatísticas, fixadas antes de ver o resultado:

1. Recorte de dez sementes = run-010 (42-51), exatamente. Não é uma amostra
   das trinta: é a campanha que existia antes da extensão. Qualquer outra
   escolha de dez seria escolher o recorte depois de ver os dados.

2. Recorte de vinte sementes = run-013 (52-71), reportado à parte. Serve a
   uma ameaça específica: a extensão de sementes é continuação determinística de
   um mesmo gerador, não reamostragem independente. Se a variabilidade das vinte
   novas fosse sistematicamente diferente da das dez antigas, as duas metades não
   seriam permutáveis e a união de trinta seria uma média de coisas distintas.
   O recorte de vinte torna essa ameaça verificável.

3. τ_b de Kendall, postos médios. Mesma decisão de RE-2 usada em todo o
   resto: os rankings têm empates, e τ_a os ignoraria.

4. Inversões contadas sobre os 55 pares fixos, não sobre "pares que mudaram
   de posição". Empate em um dos lados é contabilizado à parte, e não como
   inversão: empatar não é inverter.

5. Métrica primária `total_gco2`; secundária `gco2_por_vm`. A afirmação do
   artigo é sobre gCO2e. A secundária existe porque o achado V3 registra que os
   rankings por total em células de carga alta são contaminados por trabalho
   desigual: algoritmos que colocam menos VMs emitem menos. Se as duas métricas
   discordarem sobre a estabilidade, isso é resultado, e fica registrado.

6. Células redundantes. Quatro das dezesseis células (`*-realjan-*`) são
   idênticas por construção do dado às suas pares `*-realjun-*`. Contá-las
   infla a concordância aparente. O número de destaque usa as doze células
   distintas; as dezesseis também são reportadas, e a diferença entre as duas
   contagens fica visível.

7. Sem correção de multiplicidade. A-05 é afirmação de estabilidade, não de
   significância. Os p-valores de τ são descritivos e ficam marcados como não
   corrigidos.

8. Nenhum limiar de "estável". O artigo antigo afirmava τ = 1,0 e zero
   inversões. Um limiar frouxo inventado agora (por exemplo "τ > 0,9 é estável")
   seria escolhido depois de ver o resultado. Reporta-se τ, inversões e CV; o
   veredito da A-05 é escrito à mão contra o número antigo.

Aborta, sem aviso seguido de continuação:
  - se as células de fase 1 não tiverem exatamente 30 sementes;
  - se o recorte da run-010 não for exatamente {42..51} ou o da run-013 não for
    exatamente {52..71};
  - se alguma célula de fase 1 não tiver os 11 algoritmos nos dois recortes.

Uso (de qualquer diretório):
  python3 analysis/seeds_10_vs_30_canonico.py [--parcial]

Saídas em `out/` (ou `out-parcial/`):
  a05_tau_10_vs_30.csv     τ_b e inversões por célula e métrica
  a05_cv_por_combinacao.csv CV a 10, 20 e 30 sementes por (célula, algoritmo)
  a05_celulas_sem_30.csv    células canônicas que ficaram com 10 sementes
  a05_digest.txt

Código de saída: 0 se tudo correr; 1 se alguma verificação abortiva falhar.
"""
import itertools
import os
import sys

import numpy as np
import pandas as pd
from scipy import stats

from carga_canonica import ALGOS, carregar

HERE = os.path.dirname(os.path.abspath(__file__))
PARCIAL = "--parcial" in sys.argv
DIR = os.path.join(HERE, "out-parcial" if PARCIAL else "out")
os.makedirs(DIR, exist_ok=True)

RUN_10 = "run-010-fase1-canonica-seeds42-51"
RUN_20 = "run-013-fase1-canonica-seeds52-71"
SEEDS_10 = frozenset(range(42, 52))
SEEDS_20 = frozenset(range(52, 72))

# Recorte do cálculo antigo, mantido aqui para deixar visível a troca de população.
CELULAS_DO_TESTE_ANTIGO = ["net100-go-realjun-folgada", "pue-go-realjun-folgada"]

# Redundantes por construção do dado (C3); ver decisão 6.
CELULAS_REDUNDANTES = ["az-realjan-alta", "az-realjan-folgada",
                       "go-realjan-alta", "go-realjan-folgada"]

METRICAS = [("total_gco2", "primária"), ("gco2_por_vm", "secundária")]

linhas = []


def log(msg=""):
    print(msg)
    linhas.append(msg)


def abortar(msg):
    print(f"ABORTADO: {msg}", file=sys.stderr)
    sys.exit(1)


def postos(medias):
    """Postos médios na ordem fixa de ALGOS. Menor média = melhor = posto 1."""
    return stats.rankdata([medias[a] for a in ALGOS], method="average")


def inversoes(m10, m30):
    """Percorre os 55 pares fixos. Devolve (invertidos, empate_num_lado)."""
    inv = emp = 0
    for a, b in itertools.combinations(ALGOS, 2):
        d10, d30 = m10[a] - m10[b], m30[a] - m30[b]
        if d10 == 0 or d30 == 0:
            emp += 1
        elif (d10 > 0) != (d30 > 0):
            inv += 1
    return inv, emp


campanha = carregar(PARCIAL, log)
df = campanha.df

log()
log("== A-05. Estabilidade do ranking ao passar de 10 para 30 sementes ==")

# --- delimitação da população, e o que ela deixou de fora --------------------
fase1 = sorted(c for c, n in campanha.n_seeds.items() if n == 30)
sem30 = sorted(c for c, n in campanha.n_seeds.items() if n != 30)
if len(fase1) != 16:
    abortar(f"esperadas 16 células com 30 sementes; encontradas {len(fase1)}: {fase1}")

log(f"  células com 30 sementes (fase 1): {len(fase1)}")
log(f"  células que permanecem com 10 sementes: {len(sem30)}")
faltando_do_antigo = [c for c in CELULAS_DO_TESTE_ANTIGO if c not in fase1]
log("  As duas células do cálculo antigo — "
    + ", ".join(CELULAS_DO_TESTE_ANTIGO) + " —")
log(f"  {'NÃO estão' if faltando_do_antigo else 'estão'} entre as de 30 sementes. "
    "A pergunta canônica cobre outra população;")
log("  ver o cabeçalho deste script. A-05 não pode ser declarada preservada por")
log("  semelhança de enunciado.")

# --- verificações abortivas dos recortes -------------------------------------
for celula in fase1:
    sub = df[df["celula"] == celula]
    s10 = frozenset(sub[sub["run_id"] == RUN_10]["seed"])
    s20 = frozenset(sub[sub["run_id"] == RUN_20]["seed"])
    if s10 != SEEDS_10:
        abortar(f"{celula}: recorte da run-010 é {sorted(s10)}, esperado 42..51")
    if s20 != SEEDS_20:
        abortar(f"{celula}: recorte da run-013 é {sorted(s20)}, esperado 52..71")
    for run_id, rotulo in ((RUN_10, "run-010"), (RUN_20, "run-013")):
        algos = set(sub[sub["run_id"] == run_id]["algoritmo"])
        if algos != set(ALGOS):
            abortar(f"{celula}/{rotulo}: algoritmos {sorted(algos)} != os 11 canônicos")
log(f"  recortes verificados: 42-51 na run-010 e 52-71 na run-013, "
    f"11 algoritmos, em {len(fase1)}/{len(fase1)} células")

# --- τ_b e inversões ----------------------------------------------------------
tau_rows = []
for metrica, papel in METRICAS:
    for celula in fase1:
        sub = df[df["celula"] == celula]
        d10 = sub[sub["run_id"] == RUN_10]
        m10 = {a: d10[d10["algoritmo"] == a][metrica].mean() for a in ALGOS}
        m30 = {a: sub[sub["algoritmo"] == a][metrica].mean() for a in ALGOS}
        t, p = stats.kendalltau(postos(m10), postos(m30), variant="b")
        inv, emp = inversoes(m10, m30)
        vence10 = min(ALGOS, key=lambda a: m10[a])
        vence30 = min(ALGOS, key=lambda a: m30[a])
        tau_rows.append({
            "metrica": metrica, "papel_da_metrica": papel, "celula": celula,
            "redundante_por_construcao": celula in CELULAS_REDUNDANTES,
            "tau_b_10_vs_30": float(t), "p_nao_corrigido": float(p),
            "pares_invertidos": inv, "pares_com_empate_em_um_lado": emp,
            "pares_avaliados": len(list(itertools.combinations(ALGOS, 2))),
            "vencedor_10_seeds": vence10, "vencedor_30_seeds": vence30,
            "vencedor_mudou": vence10 != vence30,
        })
tau = pd.DataFrame(tau_rows)

for metrica, papel in METRICAS:
    sub = tau[tau["metrica"] == metrica]
    dist = sub[~sub["redundante_por_construcao"]]
    log()
    log(f"  -- {metrica} ({papel}) --")
    log(f"     16 células:  τ_b mediana = {sub['tau_b_10_vs_30'].median():.4f}; "
        f"mínima = {sub['tau_b_10_vs_30'].min():.4f}; "
        f"τ_b = 1,000 em {(sub['tau_b_10_vs_30'] >= 1.0).sum()}/16")
    log(f"     12 distintas: τ_b mediana = {dist['tau_b_10_vs_30'].median():.4f}; "
        f"mínima = {dist['tau_b_10_vs_30'].min():.4f}; "
        f"τ_b = 1,000 em {(dist['tau_b_10_vs_30'] >= 1.0).sum()}/12")
    log(f"     inversões somadas nas 12 distintas: {int(dist['pares_invertidos'].sum())} "
        f"de {int(dist['pares_avaliados'].sum())} pares")
    log(f"     células cujo primeiro colocado muda (12 distintas): "
        f"{int(dist['vencedor_mudou'].sum())}")
    piores = dist.nsmallest(3, "tau_b_10_vs_30")
    for _, r in piores.iterrows():
        log(f"       {r['celula']:22s} τ_b = {r['tau_b_10_vs_30']:+.4f}  "
            f"inversões = {r['pares_invertidos']:2d}  "
            f"1º: {r['vencedor_10_seeds']} -> {r['vencedor_30_seeds']}")

# --- coeficiente de variação --------------------------------------------------
def cv(serie):
    m = serie.mean()
    return float("nan") if m == 0 else 100.0 * serie.std(ddof=1) / m


cv_rows = []
for celula in fase1:
    sub = df[df["celula"] == celula]
    for a in ALGOS:
        sa = sub[sub["algoritmo"] == a]
        cv10 = cv(sa[sa["run_id"] == RUN_10]["total_gco2"])
        cv20 = cv(sa[sa["run_id"] == RUN_20]["total_gco2"])
        cv30 = cv(sa["total_gco2"])
        cv_rows.append({
            "celula": celula, "algoritmo": a,
            "redundante_por_construcao": celula in CELULAS_REDUNDANTES,
            "cv_pct_10_seeds": cv10, "cv_pct_20_novas": cv20, "cv_pct_30_seeds": cv30,
            "delta_cv_pp_30_menos_10": cv30 - cv10,
            "delta_cv_pp_20_menos_10": cv20 - cv10,
        })
cvdf = pd.DataFrame(cv_rows)
cvd = cvdf[~cvdf["redundante_por_construcao"]]

log()
log("  -- coeficiente de variação de total_gco2 (132 combinações distintas) --")
log(f"     CV a 10 sementes:  mediana {cvd['cv_pct_10_seeds'].median():.2f}%  "
    f"máximo {cvd['cv_pct_10_seeds'].max():.2f}%  "
    f"acima de 5%: {(cvd['cv_pct_10_seeds'] > 5).sum()}/{len(cvd)}")
log(f"     CV a 30 sementes:  mediana {cvd['cv_pct_30_seeds'].median():.2f}%  "
    f"máximo {cvd['cv_pct_30_seeds'].max():.2f}%  "
    f"acima de 5%: {(cvd['cv_pct_30_seeds'] > 5).sum()}/{len(cvd)}")
log(f"     Δ CV (30 − 10):    mediana {cvd['delta_cv_pp_30_menos_10'].median():+.2f} p.p.  "
    f"mínimo {cvd['delta_cv_pp_30_menos_10'].min():+.2f}  "
    f"máximo {cvd['delta_cv_pp_30_menos_10'].max():+.2f}")
log()
log("     Permutabilidade das duas metades (decisão 2): as vinte sementes novas")
log("     sozinhas contra as dez antigas sozinhas.")
log(f"     CV a 20 novas:     mediana {cvd['cv_pct_20_novas'].median():.2f}%  "
    f"máximo {cvd['cv_pct_20_novas'].max():.2f}%")
log(f"     Δ CV (20 − 10):    mediana {cvd['delta_cv_pp_20_menos_10'].median():+.2f} p.p.  "
    f"mínimo {cvd['delta_cv_pp_20_menos_10'].min():+.2f}  "
    f"máximo {cvd['delta_cv_pp_20_menos_10'].max():+.2f}")
log("     Um Δ mediano próximo de zero é compatível com metades permutáveis; não")
log("     é prova de independência, e não substitui a ressalva de que a extensão")
log("     é continuação determinística do mesmo gerador (R-SEEDS-ESCOPO).")
log(f"     cinco maiores CV a 30 sementes:")
for _, r in cvd.nlargest(5, "cv_pct_30_seeds").iterrows():
    log(f"       {r['celula']:22s} {r['algoritmo']:18s} "
        f"CV10 = {r['cv_pct_10_seeds']:6.2f}%  CV30 = {r['cv_pct_30_seeds']:6.2f}%  "
        f"Δ = {r['delta_cv_pp_30_menos_10']:+.2f} p.p.")

# --- saída --------------------------------------------------------------------
sem30_df = pd.DataFrame({
    "celula": sem30,
    "n_seeds": [campanha.n_seeds[c] for c in sem30],
    "estava_no_teste_antigo": [c in CELULAS_DO_TESTE_ANTIGO for c in sem30],
})
tau.to_csv(os.path.join(DIR, "a05_tau_10_vs_30.csv"), index=False)
cvdf.to_csv(os.path.join(DIR, "a05_cv_por_combinacao.csv"), index=False)
sem30_df.to_csv(os.path.join(DIR, "a05_celulas_sem_30.csv"), index=False)

log()
log("== artefatos escritos ==")
for nome in ("a05_tau_10_vs_30.csv", "a05_cv_por_combinacao.csv",
             "a05_celulas_sem_30.csv"):
    log(f"  {nome}")

with open(os.path.join(DIR, "a05_digest.txt"), "w") as fh:
    fh.write("\n".join(linhas) + "\n")
sys.exit(0)

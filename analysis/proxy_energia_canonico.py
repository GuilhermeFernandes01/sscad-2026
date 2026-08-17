#!/usr/bin/env python3
"""A-10: o custo de usar energia como proxy de carbono.

Fecha a parte da afirmação A-10 que o `correlacoes_canonicas.py` deixou aberta.
Aquele script produziu o τ_b entre o ranking por emissão e o ranking por energia
em cada célula; falta aqui o que o artigo afirma no resumo: quanto carbono a
mais se emitiria ao escolher o algoritmo que minimiza energia.

O cenário. PUE (power usage effectiveness) é a razão entre a energia total
consumida por um data center e a energia entregue aos equipamentos de
computação: um PUE de 1,5 significa que, para cada quilowatt-hora útil, meio
quilowatt-hora vai para refrigeração e perdas. As células `pue-*` invertem a
associação usual entre eficiência e limpeza, atribuindo o PUE mais alto ao data
center de menor intensidade de carbono. Com isso energia e emissão ficam
desacopladas por construção: dois algoritmos podem trocar de posição no ranking
de quilowatt-hora sem trocar no ranking de gramas de CO2 equivalente.

As três quantidades:

  (a) τ_b entre os dois rankings, nas células com PUE invertido e nas de
      referência: já fechado pelo `correlacoes_canonicas.py`, mas com o n
      de cada célula; aqui ele é recomputado no recorte pareado de 10 sementes,
      que é o único em que a comparação com o número antigo isola o desempate;
  (b) a razão de erro do proxy: emissão do algoritmo que minimiza energia
      dividida pela emissão do algoritmo que minimiza emissão, na mesma célula.
      É o "2,5 a 3,4 vezes" do resumo;
  (c) a invariância do ranking de emissão entre a célula com PUE invertido e a
      sua referência, que é o que sustenta chamar a intervenção de
      reponderação pura.

Decisões, fixadas antes de ver o resultado:

1. Recorte pareado por semente. As células `pue-*` têm 10 sementes e as de
   referência têm 30; as duas são restritas às sementes 42-51 e o script aborta
   se o recorte não for exato. Mesma disciplina de A-04, A-16, A-17 e A-19.

2. Argmin por média, e empate no argmin não é desempatado por nome. Os
   mínimos vêm de `postos_medios` e `vencedores`, as mesmas funções da
   reanálise principal. Se mais de um algoritmo empatar no mínimo de energia ou
   de emissão, a razão sai registrada como INDEFINIDA: escolher um representante
   em ordem alfabética seria deixar o identificador decidir o número publicado,
   que é a falha que esta campanha existe para corrigir.

3. A razão é ≥ 1 por construção, porque o denominador é o mínimo de emissão.
   O que ela mede é o preço, em carbono, de otimizar a métrica errada; um valor
   de 1,000 significa que os dois critérios selecionam algoritmos com a mesma
   emissão, e que naquela célula o proxy não custou nada.

4. O teste pareado entre os dois algoritmos é descritivo e não corrigido.
   Ele responde "a diferença de emissão entre o escolhido por energia e o
   escolhido por carbono é consistente entre sementes?", não uma hipótese
   confirmatória do desenho. Mesma decisão já registrada para as correlações.

5. A invariância (c) tem limiar declarado, como controle. Se a intervenção
   de PUE é reponderação pura da energia, o ranking de emissão da célula `pue-*`
   tem de ser idêntico ao da referência, τ_b = 1,000. Valor diferente é defeito,
   e o script marca FALHA sem interromper as demais saídas.

Uso:
  python3 analysis/proxy_energia_canonico.py [--parcial]

Saídas em `out/` (ou `out-parcial/`):
  a10_razao_erro_proxy.csv
  a10_tau_recorte_pareado.csv
  a10_invariancia_ranking_pue.csv
  a10_proxy_digest.txt
"""
import os
import sys

import numpy as np
import pandas as pd
from scipy import stats

from carga_canonica import ALGOS, carregar
from estatistica_comum import postos_medios, rank_biserial, vencedores, wsr_p

HERE = os.path.dirname(os.path.abspath(__file__))
PARCIAL = "--parcial" in sys.argv
DIR = os.path.join(HERE, "out-parcial" if PARCIAL else "out")
os.makedirs(DIR, exist_ok=True)

CONJ_PAREADO = frozenset(range(42, 52))
PARES = [("pue-az-realjun-folgada", "az-realjun-folgada"),
         ("pue-go-realjun-folgada", "go-realjun-folgada")]

linhas = []


def log(msg=""):
    print(msg)
    linhas.append(msg)


def abortar(msg):
    print(f"ABORTADO: {msg}", file=sys.stderr)
    sys.exit(1)


campanha = carregar(PARCIAL, log)
df = campanha.df


def recorte(celula):
    if celula not in campanha.celulas:
        abortar(f"célula ausente da campanha canônica: {celula}")
    sub = df[(df["celula"] == celula) & (df["seed"].isin(CONJ_PAREADO))]
    if frozenset(sub["seed"]) != CONJ_PAREADO:
        abortar(f"{celula}: recorte 42-51 incompleto, obtido {sorted(set(sub['seed']))}")
    return sub


log()
log("== A-10 (parte b e c). Custo de usar energia como proxy de carbono ==")
log("  PUE = power usage effectiveness, a razão entre a energia total do data")
log("  center e a energia entregue à computação. As células pue-* atribuem o PUE")
log("  mais alto ao data center mais limpo, desacoplando energia de emissão.")
log("  Ambos os lados restritos às sementes 42-51 (as células pue-* têm 10).")

reg = []
for c_pue, c_base in PARES:
    for celula in (c_base, c_pue):
        sub = recorte(celula)
        m_kwh = {a: float(sub[sub["algoritmo"] == a]["total_kwh"].mean()) for a in ALGOS}
        m_gco2 = {a: float(sub[sub["algoritmo"] == a]["total_gco2"].mean()) for a in ALGOS}
        v_kwh = vencedores(postos_medios(m_kwh))
        v_gco2 = vencedores(postos_medios(m_gco2))
        unico = len(v_kwh) == 1 and len(v_gco2) == 1
        if unico:
            a_kwh, a_gco2 = v_kwh[0], v_gco2[0]
            razao = m_gco2[a_kwh] / m_gco2[a_gco2]
            if a_kwh == a_gco2:
                p, r = np.nan, np.nan
            else:
                x = sub[sub["algoritmo"] == a_kwh].sort_values("seed")["total_gco2"].to_numpy()
                y = sub[sub["algoritmo"] == a_gco2].sort_values("seed")["total_gco2"].to_numpy()
                p, r = wsr_p(x - y), rank_biserial(x - y)
        else:
            razao, p, r = np.nan, np.nan, np.nan
        reg.append({
            "celula": celula, "papel": "pue_invertido" if celula == c_pue else "referencia",
            "par": c_pue,
            "argmin_kwh": "+".join(v_kwh), "argmin_gco2": "+".join(v_gco2),
            "argmin_unico": unico, "criterios_divergem": unico and v_kwh[0] != v_gco2[0],
            "gco2_do_argmin_kwh": m_gco2[v_kwh[0]] if len(v_kwh) == 1 else np.nan,
            "gco2_do_argmin_gco2": m_gco2[v_gco2[0]] if len(v_gco2) == 1 else np.nan,
            "razao_erro_proxy": razao, "p_bruto_wsr": p, "r_rank_biserial": r,
        })
proxy = pd.DataFrame(reg)

log()
log("  -- (b) razão de erro do proxy --")
for _, r in proxy.iterrows():
    if not r["argmin_unico"]:
        log(f"     {r['celula']:26s} argmin empatado "
            f"(kWh={r['argmin_kwh']}; gCO2e={r['argmin_gco2']}): razão INDEFINIDA")
        continue
    if not r["criterios_divergem"]:
        log(f"     {r['celula']:26s} os dois critérios escolhem {r['argmin_kwh']}: "
            f"razão = {r['razao_erro_proxy']:.3f} (proxy não custa nada nesta célula)")
        continue
    log(f"     {r['celula']:26s} energia escolhe {r['argmin_kwh']}, "
        f"carbono escolhe {r['argmin_gco2']}: "
        f"{r['gco2_do_argmin_kwh']:.0f} contra {r['gco2_do_argmin_gco2']:.0f} gCO2e, "
        f"razão = {r['razao_erro_proxy']:.3f}×  p={r['p_bruto_wsr']:.4g} (não corrigido)")

# --- (c) invariância do ranking de emissão sob PUE invertido -----------------
reg = []
falhas = 0
for c_pue, c_base in PARES:
    m_pue = {a: float(recorte(c_pue)[recorte(c_pue)["algoritmo"] == a]["total_gco2"].mean())
             for a in ALGOS}
    m_base = {a: float(recorte(c_base)[recorte(c_base)["algoritmo"] == a]["total_gco2"].mean())
              for a in ALGOS}
    p_pue = postos_medios(m_pue)
    p_base = postos_medios(m_base)
    tau = stats.kendalltau([p_pue[a] for a in ALGOS], [p_base[a] for a in ALGOS],
                           variant="b")
    ok = bool(np.isclose(tau.statistic, 1.0))
    if not ok:
        falhas += 1
    reg.append({"celula_pue": c_pue, "celula_referencia": c_base,
                "tau_b_ranking_gco2": float(tau.statistic),
                "p_nao_corrigido": float(tau.pvalue),
                "invariante": ok})
inv = pd.DataFrame(reg)

log()
log("  -- (c) invariância do ranking de emissão entre a célula pue-* e a referência --")
log("     controle com limiar declarado: reponderação pura da energia exige τ_b = 1,000")
for _, r in inv.iterrows():
    marca = "OK" if r["invariante"] else "FALHA — a intervenção NÃO é reponderação pura"
    log(f"     {r['celula_pue']:26s} × {r['celula_referencia']:22s} "
        f"τ_b = {r['tau_b_ranking_gco2']:+.4f}  [{marca}]")

# --- (a') τ_b no recorte pareado, para separar poder de desempate ------------
# O τ_b de `a10_proxy_kwh_gco2.csv` usa o n de cada célula: 30 nas de referência
# e 10 nas pue-*. Comparar aquele número com o antigo misturaria efeito do
# desempate com efeito do tamanho da amostra. Aqui os quatro τ_b são recomputados
# sobre as mesmas sementes 42-51, e é este o valor comparável com o antigo.
reg = []
for c_pue, c_base in PARES:
    for celula in (c_base, c_pue):
        sub = recorte(celula)
        m_kwh = [float(sub[sub["algoritmo"] == a]["total_kwh"].mean()) for a in ALGOS]
        m_gco2 = [float(sub[sub["algoritmo"] == a]["total_gco2"].mean()) for a in ALGOS]
        pk = postos_medios(dict(zip(ALGOS, m_kwh)))
        pg = postos_medios(dict(zip(ALGOS, m_gco2)))
        t = stats.kendalltau([pg[a] for a in ALGOS], [pk[a] for a in ALGOS], variant="b")
        reg.append({"celula": celula,
                    "papel": "pue_invertido" if celula == c_pue else "referencia",
                    "par": c_pue, "n_sementes": len(CONJ_PAREADO),
                    "tau_b_gco2_kwh_42a51": float(t.statistic),
                    "p_nao_corrigido": float(t.pvalue)})
tau_rec = pd.DataFrame(reg)
proxy = proxy.merge(tau_rec[["celula", "tau_b_gco2_kwh_42a51", "p_nao_corrigido"]],
                    on="celula", how="left")

log()
log("  -- (a') τ_b entre emissão e energia, no MESMO recorte de 10 sementes --")
log("     (o τ_b de a10_proxy_kwh_gco2.csv usa o n de cada célula, 30 nas de")
log("      referência; só este recorte isola o desempate do tamanho da amostra)")
for _, r in tau_rec.iterrows():
    log(f"     {r['celula']:26s} τ_b = {r['tau_b_gco2_kwh_42a51']:+.4f}  "
        f"p={r['p_nao_corrigido']:.4g} (não corrigido)")

tau_rec.to_csv(os.path.join(DIR, "a10_tau_recorte_pareado.csv"), index=False)
proxy.to_csv(os.path.join(DIR, "a10_razao_erro_proxy.csv"), index=False)
inv.to_csv(os.path.join(DIR, "a10_invariancia_ranking_pue.csv"), index=False)
log()
log("== artefatos escritos ==")
for nome in ("a10_razao_erro_proxy.csv", "a10_tau_recorte_pareado.csv",
             "a10_invariancia_ranking_pue.csv"):
    log(f"  {nome}")
if falhas:
    log(f"ATENÇÃO: {falhas} célula(s) reprovaram o controle de invariância (c)")

with open(os.path.join(DIR, "a10_proxy_digest.txt"), "w") as fh:
    fh.write("\n".join(linhas) + "\n")
sys.exit(0)

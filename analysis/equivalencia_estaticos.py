#!/usr/bin/env python3
"""A-15: lowest_carbon_dc contra best_fit e ffd sob perfil flat.

Fecha a afirmação A-15 do nível 3:

  "Equivalência de lowest_carbon_dc a best_fit e a ffd dentro de ±1% nas duas
   configurações flat sob carga alta."

A reanálise principal não fecha essa afirmação porque o
`tost_equivalencia_1pct.csv` cobre apenas os três migradores contra
`lowest_carbon_dc`. O contraste da A-15 é outro: `lowest_carbon_dc` contra dois
empacotadores genéricos. O número antigo (TOST p = 0,00098) veio da campanha do
artigo anterior e de um contraste que a reanálise canônica não recalcula.

Sob perfil flat a intensidade de carbono é constante em todos os data centers.
Não existe sinal geográfico a explorar, e um alocador ciente de carbono não tem
como se distinguir de um empacotador genérico. Se `lowest_carbon_dc` aparecesse
melhor sob flat, o ganho viria de outra propriedade do algoritmo, e a leitura do
artigo inteiro mudaria. A A-15 é, portanto, teste de sanidade do desenho, não
resultado positivo: o esperado é equivalência, e a hipótese de interesse é a nula
de diferença. Daí o teste de equivalência (TOST) em vez do teste de diferença,
cuja ausência de significância não seria evidência de equivalência.

Decisões fixadas antes de ver o resultado:

1. Células. `az-flat-alta` e `go-flat-alta`, as duas células flat de carga
   alta. Vêm do enunciado da afirmação, não de inspeção de resultados. Se
   qualquer uma faltar no insumo, o script falha em vez de reportar a que
   existe.

2. Contrastes. `lowest_carbon_dc` × `best_fit` e `lowest_carbon_dc` × `ffd`.
   Dois pares por célula, quatro contrastes ao todo, todos declarados aqui.

3. Braço de referência. A margem de ±1% é ancorada na média de
   `lowest_carbon_dc`, mesma convenção do `tost_equivalencia_1pct.csv`. Ancorar
   na média das duas séries faria a banda se deslocar com o próprio efeito sob
   teste.

4. Multiplicidade, declarada a priori. A família desta afirmação tem os
   quatro contrastes acima. Reporta-se o p bruto (convenção da tabela TOST já
   existente) e também o p ajustado por Holm sobre os quatro. A regra de
   veredito fica fixada aqui: a A-15 só é considerada preservada se a
   equivalência se sustentar nas duas leituras. Escolher entre bruto e ajustado
   depois de ver os dois seria escolher o veredito.

5. Degenerescência estrutural não se aplica aqui. O único par com
   degenerescência documentada é (follow_renewables, lowest_carbon_dc). Se
   `best_fit` ou `ffd` produzirem série idêntica à de `lowest_carbon_dc`, isso é
   empate empírico sob a regra RE-2: registra-se como tal, não vira "equivalente
   por construção" e não autoriza colapsar tratamentos.

6. kWh como verificação interna, não como segundo resultado. Sob flat,
   gCO2e = 475,0 × kWh exatamente, então a diferença percentual e o p em kWh têm
   de coincidir com os de gCO2e. Divergência aqui é defeito do pipeline, e o
   script falha. Só a linha em gCO2e é resultado; a de kWh é controle.

7. Piso do teste exato, por célula. Com n pares não nulos, o menor p
   unilateral exato do Wilcoxon é 1/2**n. Reportá-lo separa "não equivalente" de
   "sem poder para decidir", duas conclusões diferentes que o mesmo p grande
   pode esconder.

Uso (de qualquer diretório):
  python3 analysis/equivalencia_estaticos.py [--parcial]

Código de saída: 0 se tudo correr; 1 se uma célula declarada faltar ou se o
controle em kWh divergir; 3 sob `--parcial` com consolidado ausente (resultado
não citável), mesma convenção da reanálise principal.
"""
import os
import sys

import numpy as np
import pandas as pd

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from carga_canonica import carregar                      # noqa: E402
from estatistica_comum import holm, tost_pct             # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
PARCIAL = "--parcial" in sys.argv
OUT = os.path.join(HERE, "out-parcial" if PARCIAL else "out")
os.makedirs(OUT, exist_ok=True)

CELULAS_A15 = ["az-flat-alta", "go-flat-alta"]
REFERENCIA = "lowest_carbon_dc"
COMPARADOS = ["best_fit", "ffd"]
MARGEM = 0.01
ALFA = 0.05
RAZAO_FLAT = 475.0     # gCO2e por kWh sob perfil flat
TOL_CONTROLE = 1e-6    # tolerância relativa do controle em kWh

falhas = []
saida = []


def log(s=""):
    saida.append(str(s))


campanha = carregar(PARCIAL, log)
df = campanha.df

log()
log("== A-15. Equivalência a ±1% de lowest_carbon_dc contra best_fit e ffd, sob flat ==")
log("  Hipótese de interesse é a NULA: sob flat não há sinal geográfico a")
log("  explorar, e o alocador ciente de carbono não deveria se distinguir.")

ausentes = [c for c in CELULAS_A15 if c not in campanha.celulas]
if ausentes:
    falhas.append("células declaradas ausentes do insumo: " + ", ".join(ausentes))
    log(f"  FALHA: {falhas[-1]}")


def serie(celula, algoritmo, metrica):
    """Série ordenada por semente. A ordenação é o que torna o teste pareado
    legítimo: o par i tem de ser a mesma semente nos dois braços."""
    sub = df[(df["celula"] == celula) & (df["algoritmo"] == algoritmo)]
    return sub.sort_values("seed")[metrica].to_numpy(dtype=float)


def contraste(celula, alvo, metrica):
    """Um contraste TOST. Devolve dicionário sem veredito de família; o ajuste
    de Holm é aplicado depois, sobre a família inteira."""
    ref = serie(celula, REFERENCIA, metrica)
    alv = serie(celula, alvo, metrica)
    if len(ref) != len(alv) or len(ref) == 0:
        raise SystemExit(f"erro: {celula} {alvo}/{REFERENCIA} em {metrica} com "
                         f"{len(alv)} e {len(ref)} execuções")
    d = alv - ref
    n_nao_nulos = int(np.count_nonzero(d))
    if np.array_equal(alv, ref):
        p, nota = 0.0, "identidade_observada_em_todas_as_seeds"
    else:
        p, nota = tost_pct(alv, ref, MARGEM), "teste"
    return {"celula": celula, "referencia": REFERENCIA, "comparado": alvo,
            "metrica": metrica, "n_pares": len(ref), "n_pares_nao_nulos": n_nao_nulos,
            "media_referencia": float(ref.mean()), "media_comparado": float(alv.mean()),
            "dif_pct": float(100 * (alv.mean() - ref.mean()) / ref.mean()),
            "p_tost_bruto": float(p), "nota": nota,
            "piso_p_unilateral_exato": 1.0 / (2 ** n_nao_nulos) if n_nao_nulos else float("nan")}


presentes = [c for c in CELULAS_A15 if c in campanha.celulas]
linhas_gco2 = [contraste(c, a, "total_gco2") for c in presentes for a in COMPARADOS]
linhas_kwh = [contraste(c, a, "total_kwh") for c in presentes for a in COMPARADOS]

# --- Holm sobre a família dos quatro contrastes, declarada em (4) -------------
if linhas_gco2:
    ajust = holm([l["p_tost_bruto"] for l in linhas_gco2])
    for l, pa in zip(linhas_gco2, ajust):
        l["p_tost_holm4"] = float(pa)
        l["equivalente_bruto"] = bool(l["p_tost_bruto"] < ALFA)
        l["equivalente_holm4"] = bool(pa < ALFA)
        l["equivalente"] = bool(l["equivalente_bruto"] and l["equivalente_holm4"])

a15 = pd.DataFrame(linhas_gco2)
ctrl = pd.DataFrame(linhas_kwh)

# --- controle interno em kWh, decisão (6) ------------------------------------
log()
log("  Controle interno: sob flat gCO2e = 475,0 x kWh, logo a diferença")
log("  percentual e o p têm de coincidir entre as duas métricas.")
for lg, lk in zip(linhas_gco2, linhas_kwh):
    dif_ok = abs(lg["dif_pct"] - lk["dif_pct"]) <= TOL_CONTROLE * max(1.0, abs(lg["dif_pct"]))
    p_ok = (lg["p_tost_bruto"] == lk["p_tost_bruto"]
            or (np.isnan(lg["p_tost_bruto"]) and np.isnan(lk["p_tost_bruto"])))
    razao = (lg["media_referencia"] / lk["media_referencia"]) if lk["media_referencia"] else float("nan")
    razao_ok = abs(razao - RAZAO_FLAT) <= TOL_CONTROLE * RAZAO_FLAT
    if not (dif_ok and p_ok and razao_ok):
        falhas.append(f"controle kWh divergente em {lg['celula']}/{lg['comparado']}: "
                      f"dif_pct {lg['dif_pct']:.9g} vs {lk['dif_pct']:.9g}; "
                      f"p {lg['p_tost_bruto']:.6g} vs {lk['p_tost_bruto']:.6g}; "
                      f"razao gCO2e/kWh {razao:.9g}")
    log(f"    {lg['celula']:14s} x {lg['comparado']:9s} "
        f"razao gCO2e/kWh = {razao:.6f}  {'OK' if dif_ok and p_ok and razao_ok else 'FALHA'}")

# --- resultado ---------------------------------------------------------------
log()
log("  Contrastes em gCO2e (a margem de ±1% é ancorada na média de "
    f"{REFERENCIA}):")
for _, r in a15.iterrows():
    log(f"    {r['celula']:14s} {REFERENCIA} x {r['comparado']:9s} "
        f"n={int(r['n_pares'])} (nao nulos {int(r['n_pares_nao_nulos'])})  "
        f"dif = {r['dif_pct']:+.4f}%  "
        f"p bruto = {r['p_tost_bruto']:.6g}  p Holm(4) = {r['p_tost_holm4']:.6g}  "
        f"-> {'EQUIVALENTE' if r['equivalente'] else 'NAO equivalente'}  [{r['nota']}]")
log(f"    piso do p unilateral exato, por contraste: "
    + ", ".join(f"{r['celula']}/{r['comparado']}={r['piso_p_unilateral_exato']:.3g}"
                for _, r in a15.iterrows()))

n_eq = int(a15["equivalente"].sum()) if len(a15) else 0
log()
log(f"  Veredito mecânico: {n_eq}/{len(a15)} contrastes equivalentes a ±{MARGEM:.0%} "
    f"sob AMBAS as leituras (bruta e Holm sobre os quatro).")
log("  A regra de veredito foi fixada antes do cálculo, na decisão 4 do cabeçalho.")
if len(a15) and n_eq < len(a15):
    nao = a15[~a15["equivalente"]]
    log("  Contrastes que NÃO sustentam equivalência — precisam entrar no artigo "
        "nominalmente, não podem ser omitidos:")
    for _, r in nao.iterrows():
        log(f"    {r['celula']} x {r['comparado']}: dif = {r['dif_pct']:+.4f}%, "
            f"p bruto = {r['p_tost_bruto']:.6g}")

a15.to_csv(os.path.join(OUT, "a15_equivalencia_estaticos.csv"), index=False)
ctrl.to_csv(os.path.join(OUT, "a15_controle_kwh.csv"), index=False)
log()
log("== artefatos escritos ==")
log("  a15_equivalencia_estaticos.csv  (resultado)")
log("  a15_controle_kwh.csv            (controle interno, nao e resultado)")

if campanha.ausentes:
    log()
    log("AVISO: consolidados canônicos ausentes; execução --parcial, NÃO citável.")

texto = "\n".join(saida)
print(texto)
with open(os.path.join(OUT, "a15_digest.txt"), "w") as fh:
    fh.write(texto + "\n")

if falhas:
    print(f"\nREPROVADO: {len(falhas)} falha(s)", file=sys.stderr)
    for f in falhas:
        print(f"  {f}", file=sys.stderr)
    sys.exit(1)
if campanha.ausentes:
    print("[PARCIAL] resultado não citável — faltam consolidados canônicos",
          file=sys.stderr)
    sys.exit(3)
sys.exit(0)

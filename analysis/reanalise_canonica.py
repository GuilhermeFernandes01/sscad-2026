#!/usr/bin/env python3
"""Reanálise estatística pareada sobre a campanha canônica (binário congelado
305e2b2, sha256 36bfe40e…), em conformidade com a política de desempate RE-2,
fixada antes de qualquer resultado.

Decisões de desenho:

  1. Fonte. Lê apenas as runs canônicas, via `carga_canonica.py`. Nenhum
     número de outra origem entra aqui.
  2. Postos médios. Todo posto é midrank (`method="average"`). Atribuir a
     empatados a posição mínima, ou escolher o primeiro mínimo em ordem
     alfabética, seria um desempate incidental por identificador, que é o que a
     política RE-2 proíbe.
  3. Vencedor com empate. `melhor_*` é o conjunto de rótulos no posto mínimo.
     Quando o conjunto tem mais de um elemento, o contraste correspondente é
     reportado como empate, não resolvido por nome.
  4. Degenerescência. A classificação estrutural vem de
     `analysis/inputs/criterio-a-priori.txt`, derivada de código e configuração
     antes de qualquer resultado. Igualdade observada entre seeds nunca promove
     um par a estrutural.
  5. Famílias fixas. Nenhuma família encolhe por degenerescência. A família de
     (célula, métrica) tem sempre 55 vagas = C(11,2); a família primária tem
     sempre 3 vagas. Um contraste estruturalmente degenerado ocupa sua vaga com
     p = 1 e sai rotulado `equivalente_por_construcao`.
  6. Análise colapsada. A agregação por classe de equivalência existe apenas
     como sensibilidade explicitamente secundária (seção 7), nunca como
     primária.

O piso de resolução do teste exato é decisão de desenho, não pós-hoc, e é
propriedade de cada célula: o menor p bilateral exato do Wilcoxon signed-rank
com n pares é 2/2ⁿ, determinável antes de olhar qualquer dado.

  - Núcleo fatorial da fase 1, n = 30 (run-010 unida à run-013): piso
    2⁻²⁹ ≈ 1,86e-9; sob Holm m = 55 o menor p ajustado é ≈ 1,02e-7. A família
    de 55 tem resolução nestas células.
  - Intervenções estruturais (run-011) e dados históricos (run-012), n = 10:
    piso 2⁻⁹ = 0,001953125; sob Holm m = 55 o menor p ajustado alcançável é
    0,1074 > 0,05, de modo que nenhum par pode ser significativo ali, quaisquer
    que sejam os dados. Nessas células a família de 55 é exploratória por
    construção, e quem decide é a família primária (três migradores contra
    `lowest_carbon_dc`, m = 3, piso 0,00586).

Declarar o piso não é a redução de família que RE-2 proíbe: as 55 vagas
continuam existindo e sendo reportadas em todas as células, e o que se declara é
o poder do teste, não a exclusão de contrastes por resultado observado. A seção
0a do digest imprime o piso por célula, e a seção 1 falha se algum contraste for
declarado significativo numa célula sem resolução.

Uso (da raiz do artefato):
  python3 analysis/reanalise_canonica.py [--parcial]

Sem `--parcial`, exige que todas as runs canônicas declaradas existam e escreve
em `out/`. Com `--parcial`, roda sobre as que existirem e escreve em
`out-parcial/`, para desenvolvimento; o conteúdo de `out-parcial/` não é
artefato de resultado e não pode embasar afirmação.

Não interpreta resultados, produz tabelas.
"""
import itertools
import os
import sys

import numpy as np
import pandas as pd
from scipy import stats

# A carga das campanhas canônicas e as quatro verificações de integridade estão
# num módulo à parte porque os cálculos derivados do nível 3 precisam das mesmas.
# Ver o cabeçalho de `carga_canonica.py`.
from carga_canonica import ALGOS, CA, GB, MIGR, N_PARES, carregar
from estatistica_comum import (PAR_ESTRUTURAL, celulas_degeneradas, holm,
                               piso_wsr, postos_medios, rank_biserial, tost_pct,
                               vencedores, wsr_p)

rng = np.random.default_rng(20260806)

HERE = os.path.dirname(os.path.abspath(__file__))
EXP = os.path.normpath(os.path.join(HERE, ".."))
PARCIAL = "--parcial" in sys.argv
OUT = os.path.join(HERE, "out-parcial" if PARCIAL else "out")
os.makedirs(OUT, exist_ok=True)

digest = []


def log(s=""):
    digest.append(str(s))


# ---------------------------------------------------------------------------
# 0. Carga: consolidados canônicos + n_vms lido do config.yaml congelado
# ---------------------------------------------------------------------------
campanha = carregar(PARCIAL, log)
df = campanha.df
CELULAS = campanha.celulas
N_SEEDS = campanha.n_seeds
N_VMS = campanha.n_vms
ausentes = campanha.ausentes


log("\n== 0a. Piso de resolução do teste exato, por célula ==")
log("  Determinado por n ANTES de olhar resultados; não é seleção pós-hoc.")
for n in sorted(set(N_SEEDS.values())):
    quais = sorted(c for c in CELULAS if N_SEEDS[c] == n)
    p0 = piso_wsr(n)
    log(f"  n={n} sementes ({len(quais)} células): menor p bilateral exato = "
        f"{p0:.10g}; sob Holm m=55 o menor p ajustado alcançável é "
        f"{min(1.0, 55 * p0):.6g} -> família de 55 "
        f"{'COM' if 55 * p0 < 0.05 else 'SEM'} resolução; sob Holm m=3, "
        f"{min(1.0, 3 * p0):.6g} -> família primária "
        f"{'COM' if 3 * p0 < 0.05 else 'SEM'} resolução")
    log(f"      células: {', '.join(quais)}")
log("  As 55 vagas continuam existindo e sendo reportadas em TODAS as células; "
    "o que varia é o poder do teste, não o conjunto de contrastes (RE-2).")

# ---------------------------------------------------------------------------
# 0b. Degenerescência estrutural: lida do critério a priori, nunca inferida
# ---------------------------------------------------------------------------
estruturais, CRIT = celulas_degeneradas(EXP)
if not os.path.isfile(CRIT):
    log("  AVISO: critério a priori ausente; nenhuma célula classificada como "
        "estruturalmente degenerada (o conservador é não classificar).")


def degenerado_por_construcao(celula, a, b):
    return celula in estruturais and tuple(sorted((a, b))) == PAR_ESTRUTURAL


log(f"\n== 0b. Degenerescência estrutural (critério a priori, sem olhar resultados) ==")
log(f"  células com (follow_renewables, lowest_carbon_dc) equivalente por "
    f"construção: {len(estruturais & set(CELULAS))} de {len(CELULAS)} — "
    + (", ".join(sorted(estruturais & set(CELULAS))) or "nenhuma"))
log("  [origem: ../alocacao-identica/out/criterio-a-priori.txt]")
log("  Qualquer outra igualdade observada é EMPATE EMPÍRICO e não reduz família, "
    "não vira 'equivalente por construção' e não autoriza colapsar tratamentos.")


# ---------------------------------------------------------------------------
# Funções estatísticas
# ---------------------------------------------------------------------------
def bca_ci(d, stat=np.mean, B=10000, alpha=0.05):
    """IC BCa 95% da média das diferenças pareadas. Indefinido quando todas as
    diferenças são nulas; devolve (0, 0) e é marcado como degenerado."""
    d = np.asarray(d, dtype=float)
    n = len(d)
    if np.all(d == d[0]):
        return float(d[0]), float(d[0])
    theta = stat(d)
    boots = np.array([stat(d[rng.integers(0, n, n)]) for _ in range(B)])
    z0 = stats.norm.ppf(np.clip((boots < theta).mean(), 1e-9, 1 - 1e-9))
    jack = np.array([stat(np.delete(d, i)) for i in range(n)])
    jm = jack.mean()
    num = ((jm - jack) ** 3).sum()
    den = 6.0 * (((jm - jack) ** 2).sum()) ** 1.5
    a = num / den if den != 0 else 0.0
    zl, zu = stats.norm.ppf(alpha / 2), stats.norm.ppf(1 - alpha / 2)
    p_lo = stats.norm.cdf(z0 + (z0 + zl) / (1 - a * (z0 + zl)))
    p_hi = stats.norm.cdf(z0 + (z0 + zu) / (1 - a * (z0 + zu)))
    return float(np.quantile(boots, p_lo)), float(np.quantile(boots, p_hi))


# ---------------------------------------------------------------------------
# 1. Família exploratória: 55 pares por (célula, métrica), Holm m = 55
# ---------------------------------------------------------------------------
METRICAS = ["total_gco2", "total_kwh", "gco2_por_vm"]

log("\n== 1. Família exploratória: 55 pares por (célula, métrica), Holm m=55 ==")
linhas = []
for celula in CELULAS:
    sub = df[df["celula"] == celula]
    for metrica in METRICAS:
        serie = {a: sub[sub["algoritmo"] == a].sort_values("seed")[metrica].to_numpy()
                 for a in ALGOS}
        familia = []
        for i, j in itertools.combinations(ALGOS, 2):
            x, y = serie[i], serie[j]
            d = x - y
            deg = degenerado_por_construcao(celula, i, j)
            familia.append({
                "celula": celula, "metrica": metrica, "alg_i": i, "alg_j": j,
                "n_seeds": len(d), "media_i": x.mean(), "media_j": y.mean(),
                "dif_media": d.mean(),
                "dif_pct": 100 * d.mean() / y.mean() if y.mean() else np.nan,
                "dif_mediana": float(np.median(d)),
                "todas_dif_nulas": bool(np.all(d == 0)),
                "classe": ("equivalente_por_construcao" if deg else
                           ("empate_empirico" if np.all(d == 0) else "contraste")),
                "p_bruto": 1.0 if deg else wsr_p(d),
                "r_rank_biserial": 0.0 if deg else rank_biserial(d),
            })
        assert len(familia) == N_PARES, "a família de 55 vagas não pode encolher"
        for r, aj in zip(familia, holm([r["p_bruto"] for r in familia])):
            r["p_holm55"] = aj
            r["significativo"] = bool(aj < 0.05)
        linhas.extend(familia)

expl = pd.DataFrame(linhas)
expl.to_csv(os.path.join(OUT, "familia_exploratoria_55.csv"), index=False)
n_fam = expl.groupby(["celula", "metrica"]).size()
log(f"  {len(expl)} contrastes em {len(n_fam)} famílias; tamanho de família: "
    f"{sorted(n_fam.unique())} (fixo em {N_PARES} por desenho)")
log(f"  classes: " + ", ".join(f"{k}={v}" for k, v in
                               expl['classe'].value_counts().items()))
com_res = sorted(c for c in CELULAS if 55 * piso_wsr(N_SEEDS[c]) < 0.05)
sem_res = sorted(c for c in CELULAS if 55 * piso_wsr(N_SEEDS[c]) >= 0.05)
log(f"  significativos sob Holm m=55: {int(expl['significativo'].sum())}")
log(f"  células COM resolução nesta família ({len(com_res)}): "
    + (", ".join(com_res) or "nenhuma"))
log(f"  células SEM resolução nesta família ({len(sem_res)}, n=10 -> "
    f"55 × 0,001953 = 0,1074 > 0,05; nenhum par pode ser significativo, "
    f"quaisquer que sejam os dados): " + (", ".join(sem_res) or "nenhuma"))
if int(expl[expl["celula"].isin(sem_res)]["significativo"].sum()) != 0:
    raise SystemExit("incoerência: significância declarada em célula sem "
                     "resolução — verificar wsr_p/holm")

# ---------------------------------------------------------------------------
# 2. Família primária: 3 migradores vs lowest_carbon_dc por célula, Holm m = 3
# ---------------------------------------------------------------------------
log("\n== 2. Família primária: 3 migradores vs lowest_carbon_dc, Holm m=3 ==")
prim_linhas = []
for celula in CELULAS:
    sub = df[df["celula"] == celula]
    base = sub[sub["algoritmo"] == "lowest_carbon_dc"].sort_values("seed")["total_gco2"].to_numpy()
    familia = []
    for alg in MIGR:
        x = sub[sub["algoritmo"] == alg].sort_values("seed")["total_gco2"].to_numpy()
        d = x - base
        deg = degenerado_por_construcao(celula, alg, "lowest_carbon_dc")
        lo, hi = (0.0, 0.0) if deg else bca_ci(d)
        familia.append({
            "celula": celula, "algoritmo": alg, "n_seeds": len(d),
            "media_migrador": x.mean(), "media_lcdc": base.mean(),
            "dif_media": d.mean(), "dif_pct": 100 * d.mean() / base.mean(),
            "dif_min": d.min(), "dif_mediana": float(np.median(d)), "dif_max": d.max(),
            "ic95_bca_lo": lo, "ic95_bca_hi": hi,
            "todas_dif_nulas": bool(np.all(d == 0)),
            "classe": ("equivalente_por_construcao" if deg else
                       ("empate_empirico" if np.all(d == 0) else "contraste")),
            "p_bruto": 1.0 if deg else wsr_p(d),
            "r_rank_biserial": 0.0 if deg else rank_biserial(d),
        })
    assert len(familia) == len(MIGR), "a família primária tem sempre 3 vagas"
    for r, aj in zip(familia, holm([r["p_bruto"] for r in familia])):
        r["p_holm3"] = aj
        r["significativo"] = bool(aj < 0.05)
    prim_linhas.extend(familia)

prim = pd.DataFrame(prim_linhas)
prim.to_csv(os.path.join(OUT, "familia_primaria_holm3.csv"), index=False)
log(f"  {len(prim)} contrastes em {len(CELULAS)} famílias de 3 vagas")
log(f"  classes: " + ", ".join(f"{k}={v}" for k, v in prim['classe'].value_counts().items()))
log(f"  significativos sob Holm m=3: {int(prim['significativo'].sum())}/{len(prim)}")
log(f"  com diferença média negativa (migrador emite menos): "
    f"{int((prim['dif_pct'] < 0).sum())}/{len(prim)}")

# ---------------------------------------------------------------------------
# 3. Rankings por célula com postos médios, sem desempate incidental
# ---------------------------------------------------------------------------
log("\n== 3. Rankings por célula (postos médios; vencedor admite empate) ==")
rank_linhas, venc_linhas = [], []
for celula in CELULAS:
    sub = df[df["celula"] == celula]
    for metrica in METRICAS:
        medias = {a: sub[sub["algoritmo"] == a][metrica].mean() for a in ALGOS}
        postos = postos_medios(medias)
        v = vencedores(postos)
        for a in ALGOS:
            rank_linhas.append({"celula": celula, "metrica": metrica, "algoritmo": a,
                                "media": medias[a], "posto_medio": postos[a],
                                "vencedor": a in v})
        venc_linhas.append({"celula": celula, "metrica": metrica,
                            "vencedores": "+".join(v), "n_vencedores": len(v),
                            "houve_empate_no_topo": len(v) > 1,
                            "n_postos_distintos": len(set(postos.values()))})
rk = pd.DataFrame(rank_linhas)
rk.to_csv(os.path.join(OUT, "rankings_postos_medios.csv"), index=False)
vc = pd.DataFrame(venc_linhas)
vc.to_csv(os.path.join(OUT, "vencedores_por_celula_metrica.csv"), index=False)
log(f"  {len(vc)} combinações (célula × métrica); com empate no topo: "
    f"{int(vc['houve_empate_no_topo'].sum())}")
log(f"  combinações com ao menos um empate em qualquer posição: "
    f"{int((vc['n_postos_distintos'] < len(ALGOS)).sum())}")

# ---------------------------------------------------------------------------
# 4. Melhor carbon-aware vs melhor genérico, com empate preservado
# ---------------------------------------------------------------------------
log("\n== 4. Melhor carbon-aware vs melhor genérico (gCO2; empates preservados) ==")
cab_linhas = []
for celula in CELULAS:
    sub = df[df["celula"] == celula]
    m_ca = {a: sub[sub["algoritmo"] == a]["total_gco2"].mean() for a in CA}
    m_gb = {a: sub[sub["algoritmo"] == a]["total_gco2"].mean() for a in GB}
    v_ca, v_gb = vencedores(postos_medios(m_ca)), vencedores(postos_medios(m_gb))
    # Sob empate no topo, não há um contraste único: o teste é feito com o
    # representante apenas quando o topo é único. Com empate, o par é reportado
    # sem p: escolher um representante por nome seria desempate incidental.
    if len(v_ca) == 1 and len(v_gb) == 1:
        x = sub[sub["algoritmo"] == v_ca[0]].sort_values("seed")["total_gco2"].to_numpy()
        y = sub[sub["algoritmo"] == v_gb[0]].sort_values("seed")["total_gco2"].to_numpy()
        d = x - y
        p, r = wsr_p(d), rank_biserial(d)
        vant = 100 * (y.mean() - x.mean()) / y.mean()
    else:
        p, r, vant = np.nan, np.nan, np.nan
    cab_linhas.append({"celula": celula,
                       "melhores_ca": "+".join(v_ca), "melhores_gb": "+".join(v_gb),
                       "topo_unico": len(v_ca) == 1 and len(v_gb) == 1,
                       "vantagem_pct": vant, "p_bruto": p, "r_rank_biserial": r})
cab = pd.DataFrame(cab_linhas)
cab.to_csv(os.path.join(OUT, "melhor_ca_vs_melhor_gb.csv"), index=False)
log(f"  células com topo único nos dois grupos: {int(cab['topo_unico'].sum())}/{len(cab)}")
log(f"  células em que o melhor carbon-aware emite menos que o melhor genérico: "
    f"{int((cab['vantagem_pct'] > 0).sum())} de {int(cab['topo_unico'].sum())} com topo único")

# ---------------------------------------------------------------------------
# 5. Taxa de atendimento e ranking normalizado por VM atendida
# ---------------------------------------------------------------------------
log("\n== 5. Atendimento e ranking por VM atendida (postos médios) ==")
at_linhas = []
for celula in CELULAS:
    sub = df[df["celula"] == celula]
    g = sub.groupby("algoritmo").agg(nao_alocadas=("total_unplaced_vms", "mean"),
                                     gco2=("total_gco2", "mean"),
                                     gpv=("gco2_por_vm", "mean"))
    p_total = postos_medios(g["gco2"].to_dict())
    p_por_vm = postos_medios(g["gpv"].to_dict())
    for a in g.index:
        at_linhas.append({
            "celula": celula, "algoritmo": a,
            "atendimento_pct": 100 * (N_VMS[celula] - g.loc[a, "nao_alocadas"]) / N_VMS[celula],
            "posto_total": p_total[a], "posto_por_vm": p_por_vm[a],
            "posto_difere": p_total[a] != p_por_vm[a]})
at = pd.DataFrame(at_linhas)
at.to_csv(os.path.join(OUT, "atendimento_e_postos.csv"), index=False)
res = at.groupby("celula").agg(atend_min=("atendimento_pct", "min"),
                               atend_max=("atendimento_pct", "max"),
                               n_postos_diferem=("posto_difere", "sum")).reset_index()
res.to_csv(os.path.join(OUT, "atendimento_resumo.csv"), index=False)
log(f"  células com atendimento < 100%: {int((res['atend_min'] < 99.999).sum())}/{len(res)}")
log(f"  células em que normalizar por VM atendida muda algum posto: "
    f"{int((res['n_postos_diferem'] > 0).sum())}/{len(res)}")

# ---------------------------------------------------------------------------
# 6. Equivalência TOST ±1%
# ---------------------------------------------------------------------------
log("\n== 6. TOST ±1% (Wilcoxon unilateral deslocado) ==")


tost_linhas = []
for celula in CELULAS:
    sub = df[df["celula"] == celula]
    base = sub[sub["algoritmo"] == "lowest_carbon_dc"].sort_values("seed")["total_gco2"].to_numpy()
    for alg in MIGR:
        x = sub[sub["algoritmo"] == alg].sort_values("seed")["total_gco2"].to_numpy()
        deg = degenerado_por_construcao(celula, alg, "lowest_carbon_dc")
        if deg:
            p, nota = 0.0, "equivalente_por_construcao"
        elif np.array_equal(x, base):
            p, nota = 0.0, "identidade_observada_em_todas_as_seeds"
        else:
            p, nota = tost_pct(x, base), "teste"
        tost_linhas.append({"celula": celula, "algoritmo": alg,
                            "dif_pct": 100 * (x.mean() - base.mean()) / base.mean(),
                            "p_tost_1pct": p, "equivalente_1pct": p < 0.05,
                            "nota": nota})
tost = pd.DataFrame(tost_linhas)
tost.to_csv(os.path.join(OUT, "tost_equivalencia_1pct.csv"), index=False)
log(f"  {len(tost)} contrastes; equivalentes a ±1%: {int(tost['equivalente_1pct'].sum())}")
log(f"  notas: " + ", ".join(f"{k}={v}" for k, v in tost['nota'].value_counts().items()))

# ---------------------------------------------------------------------------
# 7. Secundária (sensibilidade): colapso por classe de equivalência estrutural
# ---------------------------------------------------------------------------
log("\n== 7. SECUNDÁRIA — colapso por classe de equivalência (sensibilidade) ==")
log("  Esta seção é EXPLICITAMENTE SECUNDÁRIA (regra 7 de RE-2). Ela NÃO "
    "substitui as famílias das seções 1 e 2, que mantêm todas as vagas.")
col_linhas = []
for celula in CELULAS:
    if celula in estruturais:
        classes = {"follow_renewables+lowest_carbon_dc": ["follow_renewables", "lowest_carbon_dc"]}
        classes.update({a: [a] for a in ALGOS if a not in ("follow_renewables", "lowest_carbon_dc")})
    else:
        classes = {a: [a] for a in ALGOS}
    n_col = len(classes)
    col_linhas.append({"celula": celula, "n_tratamentos_declarados": len(ALGOS),
                       "n_classes_estruturais": n_col,
                       "n_pares_declarados": N_PARES,
                       "n_pares_colapsados": n_col * (n_col - 1) // 2,
                       "colapsou": n_col < len(ALGOS)})
col = pd.DataFrame(col_linhas)
col.to_csv(os.path.join(OUT, "secundaria_colapso_por_classe.csv"), index=False)
log(f"  células em que o colapso estrutural reduziria a família: "
    f"{int(col['colapsou'].sum())}/{len(col)} "
    f"(de {N_PARES} para {col[col['colapsou']]['n_pares_colapsados'].max() if col['colapsou'].any() else N_PARES} pares)")
log("  Nenhum número desta seção entra nas famílias primária ou exploratória.")

with open(os.path.join(OUT, "digest.txt"), "w") as fh:
    fh.write("\n".join(digest) + "\n")
print("\n".join(digest))
print(f"\n[ok] saídas em {OUT}")
if ausentes:
    print("[PARCIAL] resultado não citável — faltam consolidados canônicos",
          file=sys.stderr)
    sys.exit(3)

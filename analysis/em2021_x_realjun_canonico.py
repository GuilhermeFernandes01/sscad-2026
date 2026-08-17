#!/usr/bin/env python3
"""A-04: dados reais de carbono contra perfil sintético.

Fecha a parte que faltava da afirmação A-04 do nível 3. As contagens de direção
e de significância saem da família primária já calculada; o que não existia era
o pareamento explícito entre cada célula `*-em2021-*` (run-012, Electricity
Maps 2021) e a `*-realjun-*` correspondente (run-010 ⊎ run-013, perfil
sintético), e a correlação entre os dois rankings. Nenhum número do cálculo
antigo é citável: ele é anterior à revisão do critério de desempate.

O script é organizado em torno de um confundimento: as duas metades da
comparação não têm o mesmo número de sementes.

  - células `*-em2021-*` (run-012): n = 10;
  - células `*-realjun-*` da fase 1 (run-010 ⊎ run-013): n = 30.

Comparar "quantos contrastes são significativos de cada lado" nessas condições
mede duas coisas somadas: a diferença entre perfil real e sintético, e a
diferença de poder estatístico. Com n = 10 o menor p bilateral exato é
0,001953125 e sob Holm m = 3 o menor p ajustado é 0,00586; com n = 30 esses
valores caem para 1,86e-09 e 5,59e-09. Um contraste pode "ganhar significância"
no lado sintético sem que nada no fenômeno tenha mudado.

Por isso a comparação em destaque é a pareada por poder: as células realjun
restritas às sementes 42-51, recomputadas com o procedimento idêntico ao da
família primária, de modo que os dois lados tenham n = 10. A versão com n = 30
é reportada em seguida, rotulada como assimétrica em poder. Reportar só a
segunda seria atribuir ao dado histórico um efeito que é do tamanho da amostra.

Decisões, fixadas antes de ver o resultado:

1. Pareamento por nome, quatro pares. `az-em2021-alta` ↔ `az-realjun-alta`,
   e assim por diante. Nenhuma célula entra ou sai depois.

2. Contrastes = família primária. Três migradores contra `lowest_carbon_dc`,
   Holm m = 3 dentro de cada célula, exatamente como na reanálise. São 12
   contrastes de cada lado, que é a base das contagens do artigo antigo
   ("9 dos 12", "11 dos 12 contra 9 dos 12").

3. Direção = sinal da diferença média pareada, não do p. Diferença
   exatamente nula é contada à parte, e não como direção preservada: empatar
   não é concordar.

4. Recomputação, não reaproveitamento. O recorte de dez sementes é
   recalculado aqui com `wsr_p`, `rank_biserial` e `holm` importados de
   `estatistica_comum`, as mesmas funções que a reanálise usa. Reimplementá-las
   abriria espaço para divergir em silêncio.

5. Degenerescência estrutural continua vindo do critério a priori. Se
   alguma das oito células estiver na lista, o contraste
   (follow_renewables, lowest_carbon_dc) entra com p = 1 e classe
   `equivalente_por_construcao`, ocupando a vaga (RE-2). O script registra
   quais das oito estão na lista, mesmo que sejam zero.

6. τ_b de Kendall entre os rankings dos 11 algoritmos, por média de
   `total_gco2`, célula real contra célula sintética, com postos médios. Também
   em duas versões: realjun a 10 e a 30 sementes.

7. Sem correção de multiplicidade entre as quatro células. Holm é dentro da
   célula, m = 3, como na reanálise. Acrescentar uma correção entre células aqui
   inventaria uma família que a reanálise não usa.

Aborta se alguma das oito células faltar, se o recorte de 42-51 nas células
realjun não for exato, ou se os 11 algoritmos não estiverem presentes.

Uso:
  python3 analysis/em2021_x_realjun_canonico.py [--parcial]

Saídas em `out/` (ou `out-parcial/`):
  a04_contrastes_pareados.csv   os 12 contrastes, lado a lado, nas duas versões
  a04_tau_ranking.csv           τ_b por par de células
  a04_digest.txt
"""
import os
import sys

import numpy as np
import pandas as pd
from scipy import stats

from carga_canonica import ALGOS, MIGR, carregar
from estatistica_comum import (PAR_ESTRUTURAL, celulas_degeneradas, holm,
                               piso_wsr, rank_biserial, wsr_p)

HERE = os.path.dirname(os.path.abspath(__file__))
EXP = os.path.normpath(os.path.join(HERE, ".."))
PARCIAL = "--parcial" in sys.argv
DIR = os.path.join(HERE, "out-parcial" if PARCIAL else "out")
os.makedirs(DIR, exist_ok=True)

PARES = [("az-em2021-alta", "az-realjun-alta"),
         ("az-em2021-folgada", "az-realjun-folgada"),
         ("go-em2021-alta", "go-realjun-alta"),
         ("go-em2021-folgada", "go-realjun-folgada")]
SEEDS_PAREADAS = frozenset(range(42, 52))
BASE = "lowest_carbon_dc"
METRICA = "total_gco2"

linhas = []


def log(msg=""):
    print(msg)
    linhas.append(msg)


def abortar(msg):
    print(f"ABORTADO: {msg}", file=sys.stderr)
    sys.exit(1)


campanha = carregar(PARCIAL, log)
df = campanha.df
estruturais, crit = celulas_degeneradas(EXP)

log()
log("== A-04. Electricity Maps 2021 contra perfil sintético realjun ==")

envolvidas = [c for par in PARES for c in par]
faltando = [c for c in envolvidas if c not in campanha.celulas]
if faltando:
    abortar(f"células ausentes da campanha canônica: {faltando}")

log(f"  4 pares de células; n por célula: "
    + ", ".join(f"{c}={campanha.n_seeds[c]}" for c in envolvidas))
log(f"  piso do teste exato: n=10 -> {piso_wsr(10):.10g} (Holm m=3: "
    f"{3 * piso_wsr(10):.6g}); n=30 -> {piso_wsr(30):.10g} (Holm m=3: "
    f"{3 * piso_wsr(30):.6g})")
deg_envolvidas = sorted(set(envolvidas) & estruturais)
log(f"  células destas oito com degenerescência estrutural: "
    f"{', '.join(deg_envolvidas) if deg_envolvidas else 'nenhuma'} "
    f"[origem: critique-checks/alocacao-identica/out/criterio-a-priori.txt]")


def familia_primaria(celula, sementes=None):
    """Recalcula a família primária de uma célula, opcionalmente restrita.

    `sementes=None` usa todas as da célula. Devolve três registros na ordem de
    MIGR, já com Holm m=3 aplicado sobre as três vagas.
    """
    sub = df[df["celula"] == celula]
    if sementes is not None:
        sub = sub[sub["seed"].isin(sementes)]
        obtidas = frozenset(sub["seed"])
        if obtidas != sementes:
            abortar(f"{celula}: recorte pedido {sorted(sementes)}, obtido {sorted(obtidas)}")
    algos = set(sub["algoritmo"])
    if algos != set(ALGOS):
        abortar(f"{celula}: algoritmos {sorted(algos)} != os 11 canônicos")
    base = sub[sub["algoritmo"] == BASE].sort_values("seed")[METRICA].to_numpy()
    fam = []
    for alg in MIGR:
        x = sub[sub["algoritmo"] == alg].sort_values("seed")[METRICA].to_numpy()
        d = x - base
        deg = celula in estruturais and tuple(sorted((alg, BASE))) == PAR_ESTRUTURAL
        fam.append({
            "algoritmo": alg, "n_seeds": len(d),
            "dif_media": float(d.mean()),
            "dif_pct": float(100 * d.mean() / base.mean()),
            "classe": ("equivalente_por_construcao" if deg else
                       ("empate_empirico" if np.all(d == 0) else "contraste")),
            "p_bruto": 1.0 if deg else float(wsr_p(d)),
            "r_rank_biserial": 0.0 if deg else float(rank_biserial(d)),
        })
    assert len(fam) == 3, "a família primária tem sempre 3 vagas"
    for r, aj in zip(fam, holm([r["p_bruto"] for r in fam])):
        r["p_holm3"] = float(aj)
        r["significativo"] = bool(aj < 0.05)
    return fam


def medias(celula, sementes=None):
    sub = df[df["celula"] == celula]
    if sementes is not None:
        sub = sub[sub["seed"].isin(sementes)]
    return {a: sub[sub["algoritmo"] == a][METRICA].mean() for a in ALGOS}


def sinal(x):
    return 0 if x == 0 else (1 if x > 0 else -1)


# --- os 12 contrastes, nas duas versões --------------------------------------
registros = []
for c_real, c_sint in PARES:
    fam_real = familia_primaria(c_real)
    fam_pareada = familia_primaria(c_sint, SEEDS_PAREADAS)
    fam_cheia = familia_primaria(c_sint)
    for r, p10, p30 in zip(fam_real, fam_pareada, fam_cheia):
        registros.append({
            "celula_real": c_real, "celula_sintetica": c_sint,
            "algoritmo": r["algoritmo"],
            "n_real": r["n_seeds"], "n_sintetica_pareada": p10["n_seeds"],
            "n_sintetica_cheia": p30["n_seeds"],
            "dif_pct_real": r["dif_pct"],
            "dif_pct_sintetica_10": p10["dif_pct"],
            "dif_pct_sintetica_30": p30["dif_pct"],
            "p_holm3_real": r["p_holm3"],
            "p_holm3_sintetica_10": p10["p_holm3"],
            "p_holm3_sintetica_30": p30["p_holm3"],
            "sig_real": r["significativo"],
            "sig_sintetica_10": p10["significativo"],
            "sig_sintetica_30": p30["significativo"],
            "classe_real": r["classe"], "classe_sintetica_10": p10["classe"],
            "direcao_preservada_vs_10": sinal(r["dif_pct"]) == sinal(p10["dif_pct"])
                                        and sinal(r["dif_pct"]) != 0,
            "alguma_dif_nula": sinal(r["dif_pct"]) == 0 or sinal(p10["dif_pct"]) == 0,
            "r_rank_biserial_real": r["r_rank_biserial"],
        })
con = pd.DataFrame(registros)

n = len(con)
dir_ok = int(con["direcao_preservada_vs_10"].sum())
# Não-preservado tem duas causas distintas, e confundi-las inflaria a inversão:
# sinal oposto dos dois lados é discordância; zero de um lado é empate.
invertidos = con[~con["direcao_preservada_vs_10"] & ~con["alguma_dif_nula"]]
empatados = con[con["alguma_dif_nula"]]
log()
log(f"  -- direção, {n} contrastes (3 migradores x 4 células) --")
log(f"     direção preservada entre real e sintético (ambos com n=10): {dir_ok}/{n}")
log(f"     inversões de sinal propriamente ditas: {len(invertidos)}/{n}")
for _, r in invertidos.iterrows():
    log(f"       {r['celula_real']:20s} {r['algoritmo']:18s} "
        f"real {r['dif_pct_real']:+.3f}%  sintético(10) {r['dif_pct_sintetica_10']:+.3f}%")
log(f"     contrastes com diferença média exatamente nula em um dos lados: "
    f"{len(empatados)}/{n} — não contam como direção preservada nem como inversão")
for _, r in empatados.iterrows():
    log(f"       {r['celula_real']:20s} {r['algoritmo']:18s} "
        f"real {r['dif_pct_real']:+.3f}% ({r['classe_real']})  "
        f"sintético(10) {r['dif_pct_sintetica_10']:+.3f}% ({r['classe_sintetica_10']})")

log()
log(f"  -- significância sob Holm m=3 --")
log(f"     PAREADA POR PODER (n=10 dos dois lados), que é a comparação de destaque:")
log(f"       real: {int(con['sig_real'].sum())}/{n}   "
    f"sintético: {int(con['sig_sintetica_10'].sum())}/{n}")
log(f"     ASSIMÉTRICA (real n=10 contra sintético n=30), reportada para registro:")
log(f"       real: {int(con['sig_real'].sum())}/{n}   "
    f"sintético: {int(con['sig_sintetica_30'].sum())}/{n}")
mudou = con[con["sig_sintetica_10"] != con["sig_sintetica_30"]]
log(f"     contrastes do lado sintético cuja significância muda só por ir de 10 "
    f"para 30 sementes: {len(mudou)}")
for _, r in mudou.iterrows():
    log(f"       {r['celula_sintetica']:20s} {r['algoritmo']:18s} "
        f"n=10 p={r['p_holm3_sintetica_10']:.4g} ({'sig' if r['sig_sintetica_10'] else 'não'})"
        f" -> n=30 p={r['p_holm3_sintetica_30']:.4g} "
        f"({'sig' if r['sig_sintetica_30'] else 'não'})")
log("     Essa contagem é a medida direta do confundimento: onde ela muda, a")
log("     contagem 'x de 12' estaria medindo poder, não perfil.")
if len(mudou) == 0:
    log("     Deu zero. O confundimento era uma ameaça real ao desenho e não se")
    log("     materializou NESTES doze contrastes: as duas versões coincidem, e a")
    log("     diferença entre real e sintético não é atribuível ao tamanho da")
    log("     amostra. Isso vale só aqui — o pareamento continua obrigatório em")
    log("     qualquer outra comparação entre células de n diferente.")

# --- τ_b entre os rankings ----------------------------------------------------
tau_rows = []
for c_real, c_sint in PARES:
    mr = medias(c_real)
    m10 = medias(c_sint, SEEDS_PAREADAS)
    m30 = medias(c_sint)
    pr = stats.rankdata([mr[a] for a in ALGOS], method="average")
    p10 = stats.rankdata([m10[a] for a in ALGOS], method="average")
    p30 = stats.rankdata([m30[a] for a in ALGOS], method="average")
    t10 = stats.kendalltau(pr, p10, variant="b")
    t30 = stats.kendalltau(pr, p30, variant="b")
    venc_r = sorted(a for a in ALGOS if mr[a] == min(mr.values()))
    venc_10 = sorted(a for a in ALGOS if m10[a] == min(m10.values()))
    venc_30 = sorted(a for a in ALGOS if m30[a] == min(m30.values()))
    tau_rows.append({
        "celula_real": c_real, "celula_sintetica": c_sint,
        "tau_b_vs_sintetica_10": float(t10.statistic),
        "p_nao_corrigido_10": float(t10.pvalue),
        "tau_b_vs_sintetica_30": float(t30.statistic),
        "p_nao_corrigido_30": float(t30.pvalue),
        "primeiro_real": "|".join(venc_r),
        "primeiro_sintetica_10": "|".join(venc_10),
        "primeiro_sintetica_30": "|".join(venc_30),
        "primeiro_colocado_muda_vs_10": venc_r != venc_10,
    })
tau = pd.DataFrame(tau_rows)

log()
log("  -- τ_b entre o ranking dos 11 algoritmos, real contra sintético --")
for _, r in tau.iterrows():
    log(f"     {r['celula_real']:20s} τ_b(10) = {r['tau_b_vs_sintetica_10']:+.4f}   "
        f"τ_b(30) = {r['tau_b_vs_sintetica_30']:+.4f}   "
        f"1º: {r['primeiro_real']} vs {r['primeiro_sintetica_10']}"
        f"{'  [TROCA]' if r['primeiro_colocado_muda_vs_10'] else ''}")
log(f"     τ_b(10): mínimo {tau['tau_b_vs_sintetica_10'].min():.4f}, "
    f"máximo {tau['tau_b_vs_sintetica_10'].max():.4f}")
log(f"     células em que o primeiro colocado troca: "
    f"{int(tau['primeiro_colocado_muda_vs_10'].sum())}/4")
log("     Os p-valores de τ são descritivos e NÃO corrigidos: nenhuma afirmação")
log("     do artigo depende de 'τ significativamente diferente de zero'.")

con.to_csv(os.path.join(DIR, "a04_contrastes_pareados.csv"), index=False)
tau.to_csv(os.path.join(DIR, "a04_tau_ranking.csv"), index=False)
log()
log("== artefatos escritos ==")
for nome in ("a04_contrastes_pareados.csv", "a04_tau_ranking.csv"):
    log(f"  {nome}")

with open(os.path.join(DIR, "a04_digest.txt"), "w") as fh:
    fh.write("\n".join(linhas) + "\n")
sys.exit(0)

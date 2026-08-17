#!/usr/bin/env python3
"""Extração das afirmações restantes do nível 3.

As afirmações remanescentes do nível 3 estão a uma extração de distância dos
artefatos canônicos, mas cada uma delas foi contada, no artigo antigo, sobre uma
população específica; repetir o número sobre outra população daria uma
comparação com aparência de rigor e sem conteúdo. Este script fixa as
populações, extrai os valores dos artefatos canônicos e, quando a população
antiga difere da canônica em tamanho de amostra, recomputa o recorte pareado de
sementes 42-51, para que a diferença observada seja atribuível ao desempate e
não ao número de sementes.

Decisões pré-declaradas, antes de olhar qualquer resultado:

  D1  As duas populações do artigo antigo (a lista de 12 células e a de 22) são
      reproduzidas verbatim do cálculo antigo, não redigitadas de memória.
      As 30 células canônicas menos as 4 réplicas `realjan` (bit-idênticas às
      `realjun`, A-23) e menos as 4 células `em2021` (contadas em separado pelo
      texto antigo) dão exatamente essas 22. O script aborta se não der.

  D2  Nenhum teste é recomputado a partir do bruto quando o artefato canônico já
      o carrega. A única exceção é o recorte pareado de sementes 42-51, usado
      onde a célula canônica tem n = 30 e a antiga tinha n = 10; nesses casos os
      dois valores são reportados lado a lado e o recorte usa as mesmas funções
      de `estatistica_comum`.

  D3  Toda contagem sobre limiar vem acompanhada da lista nominal das células que
      falham. Contagem sem os nomes esconde qual configuração mudou de lado.

  D4  Holm é reaproveitado da coluna do artefato sempre que existe. Onde a
      afirmação exige uma família que nenhum artefato declara (A-20), a correção
      é calculada aqui e rotulada como número novo, não como reprodução.

  D5  Os números antigos são citados como estão registrados no artigo; nenhum
      deles é recomputado. A campanha antiga é anterior à revisão do critério
      de desempate e seus valores só existem como registro histórico.

  D6  O intervalo BCa não é recomputado no recorte pareado. O reamostrador
      canônico está em `reanalise_canonica.py`; reimplementá-lo aqui criaria uma
      segunda versão livre para divergir em silêncio. Onde o BCa aparece, é o da
      célula canônica inteira, e isso é dito.

Saída:
  out/nivel3_extracoes.txt      digest legível, uma seção por afirmação
  out/a06_geo3_recorte.csv      vantagem geo3 e base, canônica e pareada
  out/a11_proxy_12_referencia.csv
"""
import os
import sys

import numpy as np
import pandas as pd
from scipy import stats

from carga_canonica import ALGOS, CA, GB, carregar
from estatistica_comum import (holm, piso_wsr, postos_medios, rank_biserial,
                               vencedores, wsr_p)

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")

# D1: populações do artigo antigo, verbatim
CELULAS_12 = [
    "az-realjun-folgada", "az-realjun-alta", "az-flat-folgada", "az-flat-alta",
    "az-anticorr-folgada", "az-anticorr-alta", "go-realjun-folgada", "go-realjun-alta",
    "go-flat-folgada", "go-flat-alta", "go-anticorr-folgada", "go-anticorr-alta",
]
CELULAS_5 = [
    "net100-az-realjun-folgada", "net100-go-realjun-folgada",
    "net100-az-anticorr-folgada", "net100-az-realjun-alta",
    "geo3-az-realjun-folgada", "geo3-go-realjun-folgada",
    "hetcap-az-realjun-folgada", "hetcap-az-anticorr-folgada",
    "pue-az-realjun-folgada", "pue-go-realjun-folgada",
]
CELULAS_22 = CELULAS_12 + CELULAS_5
BASES = {
    "net100-az-realjun-folgada": "az-realjun-folgada",
    "net100-go-realjun-folgada": "go-realjun-folgada",
    "net100-az-anticorr-folgada": "az-anticorr-folgada",
    "net100-az-realjun-alta": "az-realjun-alta",
    "geo3-az-realjun-folgada": "az-realjun-folgada",
    "geo3-go-realjun-folgada": "go-realjun-folgada",
    "hetcap-az-realjun-folgada": "az-realjun-folgada",
    "hetcap-az-anticorr-folgada": "az-anticorr-folgada",
    "pue-az-realjun-folgada": "az-realjun-folgada",
    "pue-go-realjun-folgada": "go-realjun-folgada",
}
SEEDS_PAREADAS = list(range(42, 52))
MIGR = ["cnemesis", "follow_renewables", "followme_s"]

digest = []


def log(s=""):
    print(s)
    digest.append(s)


def ler(nome):
    caminho = os.path.join(OUT, nome)
    if not os.path.isfile(caminho):
        print(f"ABORTA: artefato canônico ausente — out/{nome}", file=sys.stderr)
        sys.exit(2)
    return pd.read_csv(caminho)


def pct(x):
    return f"{x:+.4f}%".replace(".", ",")


def num(x, casas=3):
    return f"{x:.{casas}f}".replace(".", ",")


log("== extracao_nivel3.py — afirmações restantes do nível 3 ==\n")
camp = carregar(parcial=False, log=log)
df = camp.df

# D1: verificação das populações
faltando = [c for c in CELULAS_22 if c not in camp.celulas]
if faltando:
    print(f"ABORTA: células do recorte antigo ausentes na campanha canônica: {faltando}",
          file=sys.stderr)
    sys.exit(3)
derivadas = sorted(set(camp.celulas)
                   - {c for c in camp.celulas if "realjan" in c or "em2021" in c})
if sorted(CELULAS_22) != derivadas:
    print("ABORTA: as 30 células canônicas menos `realjan` e `em2021` não coincidem "
          f"com as 22 do script antigo.\n  derivadas={derivadas}\n  antigas={sorted(CELULAS_22)}",
          file=sys.stderr)
    sys.exit(3)
log(f"\n  recorte de 22 configurações não redundantes conferido: 30 células "
    f"- 4 `realjan` - 4 `em2021` = {len(derivadas)}")
log(f"  recorte de 12 configurações de referência: {len(CELULAS_12)}")


def recorte_pareado(celula, alg_x, alg_y, metrica="total_gco2"):
    """Contraste pareado nas sementes 42-51, com as funções canônicas."""
    sub = df[(df["celula"] == celula) & (df["seed"].isin(SEEDS_PAREADAS))]
    x = sub[sub["algoritmo"] == alg_x].sort_values("seed")[metrica].to_numpy()
    y = sub[sub["algoritmo"] == alg_y].sort_values("seed")[metrica].to_numpy()
    if len(x) != len(SEEDS_PAREADAS) or len(y) != len(SEEDS_PAREADAS):
        print(f"ABORTA: recorte 42-51 incompleto em {celula} "
              f"({alg_x}={len(x)}, {alg_y}={len(y)})", file=sys.stderr)
        sys.exit(3)
    d = x - y
    return {"dif_media": float(d.mean()), "dif_pct": float(100 * d.mean() / y.mean()),
            "p_bruto": wsr_p(d), "r": rank_biserial(d)}


def vantagem_ca_gb(celula, seeds=None):
    """Melhor CA contra melhor genérico, com a mesma regra da reanálise."""
    sub = df[df["celula"] == celula]
    if seeds is not None:
        sub = sub[sub["seed"].isin(seeds)]
    m_ca = {a: sub[sub["algoritmo"] == a]["total_gco2"].mean() for a in CA}
    m_gb = {a: sub[sub["algoritmo"] == a]["total_gco2"].mean() for a in GB}
    v_ca, v_gb = vencedores(postos_medios(m_ca)), vencedores(postos_medios(m_gb))
    if len(v_ca) != 1 or len(v_gb) != 1:
        return {"melhor_ca": "+".join(v_ca), "melhor_gb": "+".join(v_gb),
                "vantagem_pct": np.nan, "p_bruto": np.nan, "r": np.nan, "n": len(sub) // len(ALGOS)}
    x = sub[sub["algoritmo"] == v_ca[0]].sort_values("seed")["total_gco2"].to_numpy()
    y = sub[sub["algoritmo"] == v_gb[0]].sort_values("seed")["total_gco2"].to_numpy()
    d = x - y
    return {"melhor_ca": v_ca[0], "melhor_gb": v_gb[0],
            "vantagem_pct": float(100 * (y.mean() - x.mean()) / y.mean()),
            "p_bruto": wsr_p(d), "r": rank_biserial(d), "n": len(x)}


prim = ler("familia_primaria_holm3.csv")
expl = ler("familia_exploratoria_55.csv")
rk = ler("rankings_postos_medios.csv")
vc = ler("vencedores_por_celula_metrica.csv")
cab = ler("melhor_ca_vs_melhor_gb.csv")
tost = ler("tost_equivalencia_1pct.csv")
tau7 = ler("a07_estabilidade_ranking.csv")
atend = ler("atendimento_e_postos.csv")

# ---------------------------------------------------------------------------
log("\n\n== A-01 — ganho máximo com migração sob carga alta e sinal variável ==")
log("antigo: -4,76% e -9,84% (Azure); -1,27% e -1,71% (Google); p_holm <= 0,012; "
    "r = -1,00 em três; BCa [-3,167; -2,607] em az-realjun-alta")
A01 = ["az-realjun-alta", "az-anticorr-alta", "go-realjun-alta", "go-anticorr-alta"]
for celula in A01:
    r = prim[(prim["celula"] == celula) & (prim["algoritmo"] == "follow_renewables")].iloc[0]
    par = recorte_pareado(celula, "follow_renewables", "lowest_carbon_dc")
    log(f"  {celula}: n={int(r['n_seeds'])} dif={pct(r['dif_pct'])} "
        f"p_holm3={r['p_holm3']:.3e} r={num(r['r_rank_biserial'])} "
        f"BCa=[{num(r['ic95_bca_lo'],1)}; {num(r['ic95_bca_hi'],1)}] gCO2e")
    log(f"      recorte 42-51 (n=10): dif={pct(par['dif_pct'])} "
        f"p_bruto={par['p_bruto']:.3e} r={num(par['r'])}")
log(f"  piso do teste exato: n=30 -> {piso_wsr(30):.3e}; n=10 -> {piso_wsr(10):.3e}")

# ---------------------------------------------------------------------------
log("\n\n== A-02 — equivalência sob WAN de 100 Mb/s em az-realjun-alta ==")
log("antigo: -0,08% a -0,10%; TOST p = 0,00098")
sub = tost[tost["celula"] == "net100-az-realjun-alta"]
for _, r in sub.iterrows():
    log(f"  {r['algoritmo']}: dif={pct(r['dif_pct'])} p_tost={r['p_tost_1pct']:.3e} "
        f"equivalente={bool(r['equivalente_1pct'])} nota={r['nota']}")
log(f"  equivalentes dentro de +-1%: {int(sub['equivalente_1pct'].sum())}/{len(sub)}")

# ---------------------------------------------------------------------------
log("\n\n== A-03 — cnemesis supera lowest_carbon_dc em 16 das 22 configurações ==")
log("antigo: 16/22 significativas; go-anticorr-alta inverte para +0,20% (não significativo)")
c3 = prim[(prim["algoritmo"] == "cnemesis") & (prim["celula"].isin(CELULAS_22))]
favoravel = c3[(c3["significativo"]) & (c3["dif_media"] < 0)]
contra = c3[(c3["significativo"]) & (c3["dif_media"] > 0)]
nulo = c3[~c3["significativo"]]
log(f"  significativas E favoráveis a cnemesis: {len(favoravel)}/{len(c3)}")
log(f"  significativas E CONTRA cnemesis: {len(contra)}/{len(c3)} -> "
    + "; ".join(f"{r['celula']} {pct(r['dif_pct'])}" for _, r in contra.iterrows()))
log(f"  não significativas: {len(nulo)}/{len(c3)} -> "
    + "; ".join(f"{r['celula']} {pct(r['dif_pct'])} p={r['p_holm3']:.3g}"
                for _, r in nulo.iterrows()))
for celula in ["go-anticorr-alta", "geo3-go-realjun-folgada"]:
    r = c3[c3["celula"] == celula].iloc[0]
    log(f"  {celula}: dif={pct(r['dif_pct'])} BCa=[{num(r['ic95_bca_lo'],1)}; "
        f"{num(r['ic95_bca_hi'],1)}] p_holm3={r['p_holm3']:.3g}")

# ---------------------------------------------------------------------------
log("\n\n== A-06 — vantagem ciente de carbono com 3 data centers ==")
log("antigo: 60,99% -> 8,33% (Azure, p = 0,0020); 66,97% -> 0,77% (Google, p = 0,63)")
linhas06 = []
for geo in ["geo3-az-realjun-folgada", "geo3-go-realjun-folgada"]:
    base = BASES[geo]
    v_geo = vantagem_ca_gb(geo)
    v_base_full = vantagem_ca_gb(base)
    v_base_par = vantagem_ca_gb(base, SEEDS_PAREADAS)
    log(f"  {base} (n={v_base_full['n']}): vantagem={pct(v_base_full['vantagem_pct'])} "
        f"[{v_base_full['melhor_ca']} x {v_base_full['melhor_gb']}] p={v_base_full['p_bruto']:.3e}")
    log(f"  {base} recorte 42-51 (n={v_base_par['n']}): "
        f"vantagem={pct(v_base_par['vantagem_pct'])} "
        f"[{v_base_par['melhor_ca']} x {v_base_par['melhor_gb']}] p={v_base_par['p_bruto']:.3e}")
    log(f"  {geo} (n={v_geo['n']}): vantagem={pct(v_geo['vantagem_pct'])} "
        f"[{v_geo['melhor_ca']} x {v_geo['melhor_gb']}] p={v_geo['p_bruto']:.3e} "
        f"r={num(v_geo['r'])} -> significativa a 0,05: {v_geo['p_bruto'] < 0.05}")
    for rot, v in [("base_n_cheio", v_base_full), ("base_42_51", v_base_par), ("geo3", v_geo)]:
        linhas06.append({"celula": base if rot != "geo3" else geo, "recorte": rot, **v})
pd.DataFrame(linhas06).to_csv(os.path.join(OUT, "a06_geo3_recorte.csv"), index=False)

# ---------------------------------------------------------------------------
log("\n\n== A-07 — estabilidade do ranking entre configurações ==")
log("antigo: tau mediana 0,771, mínimo 0,299 nas 22 configurações; 13 pares-testemunha "
    "(8 só entre estáticos); PUE tau=1,000; net100 realjun folgada 1,000, anticorr-folgada "
    "0,926, carga alta 0,964; geo3 0,697; hetcap 0,881/0,963")
t22 = tau7[tau7["celula_i"].isin(CELULAS_22) & tau7["celula_j"].isin(CELULAS_22)]
log(f"  pares de células: {len(t22)} (esperado C(22,2) = {22 * 21 // 2})")
log(f"  tau_b: mediana={num(t22['tau_b'].median(),4)} mínimo={num(t22['tau_b'].min(),4)} "
    f"máximo={num(t22['tau_b'].max(),4)}")
menores = t22.nsmallest(5, "tau_b")
log("  cinco menores: " + "; ".join(
    f"{r['celula_i']}×{r['celula_j']}={num(r['tau_b'],3)}" for _, r in menores.iterrows()))
log(f"  todas as 435 combinações das 30 células: mediana={num(tau7['tau_b'].median(),4)} "
    f"mínimo={num(tau7['tau_b'].min(),4)}")
log("  célula com intervenção × sua base:")
for celula in CELULAS_5:
    base = BASES[celula]
    m = tau7[((tau7["celula_i"] == celula) & (tau7["celula_j"] == base))
             | ((tau7["celula_i"] == base) & (tau7["celula_j"] == celula))]
    log(f"    {celula} × {base}: tau_b={num(m.iloc[0]['tau_b'],4)}")
# pares-testemunha de inversão (mesma definição do script antigo)
e22 = expl[expl["celula"].isin(CELULAS_22) & expl["significativo"]]
for metrica in ["total_gco2", "gco2_por_vm"]:
    mp = e22[e22["metrica"] == metrica]
    tes, so_est = [], 0
    for (i, j), g in mp.groupby(["alg_i", "alg_j"]):
        if (g["dif_media"] < 0).any() and (g["dif_media"] > 0).any():
            tes.append((i, j))
            if i not in MIGR and j not in MIGR:
                so_est += 1
    log(f"  pares-testemunha de inversão ({metrica}, 22 células): {len(tes)} pares "
        f"({so_est} só entre estáticos)")
    log("    " + "; ".join(f"{i}×{j}" for i, j in tes))

log("\n  -- A-07b: recorte pareado de sementes 42-51 --")
log("  Motivo: no artefato canônico cada célula entra com o SEU n (30 na fase 1, 10 nas")
log("  intervenções), de modo que todo par que atravessa esses dois grupos mistura efeito")
log("  de desempate com efeito de tamanho de amostra. O mesmo confundimento já foi")
log("  medido em A-10, onde o controle pareado do PUE dá 1,0000 e o não pareado dá 0,9630.")
medias_par = {}
for celula in CELULAS_22:
    sub = df[(df["celula"] == celula) & (df["seed"].isin(SEEDS_PAREADAS))]
    medias_par[celula] = np.array(
        [sub[sub["algoritmo"] == a]["total_gco2"].mean() for a in ALGOS])
par_taus = []
for i in range(len(CELULAS_22)):
    for j in range(i + 1, len(CELULAS_22)):
        t = stats.kendalltau(medias_par[CELULAS_22[i]], medias_par[CELULAS_22[j]],
                             variant="b")
        par_taus.append({"celula_i": CELULAS_22[i], "celula_j": CELULAS_22[j],
                         "tau_b": float(t.statistic), "p_nao_corrigido": float(t.pvalue)})
pt = pd.DataFrame(par_taus)
pt.to_csv(os.path.join(OUT, "a07_tau_recorte_pareado.csv"), index=False)
log(f"  {len(pt)} pares em 42-51: mediana={num(pt['tau_b'].median(),4)} "
    f"mínimo={num(pt['tau_b'].min(),4)} máximo={num(pt['tau_b'].max(),4)}")
log("  cinco menores: " + "; ".join(
    f"{r['celula_i']}×{r['celula_j']}={num(r['tau_b'],3)}"
    for _, r in pt.nsmallest(5, "tau_b").iterrows()))
log("  célula com intervenção × sua base, pareado em 42-51:")
for celula in CELULAS_5:
    base = BASES[celula]
    m = pt[((pt["celula_i"] == celula) & (pt["celula_j"] == base))
           | ((pt["celula_i"] == base) & (pt["celula_j"] == celula))].iloc[0]
    nao_par = tau7[((tau7["celula_i"] == celula) & (tau7["celula_j"] == base))
                   | ((tau7["celula_i"] == base) & (tau7["celula_j"] == celula))].iloc[0]
    log(f"    {celula} × {base}: pareado={num(m['tau_b'],4)} "
        f"(não pareado={num(nao_par['tau_b'],4)})")

# ---------------------------------------------------------------------------
log("\n\n== A-08 — posição de ffd e bfd entre os estáticos ==")
log("antigo: em go-flat-folgada ffd supera lowest_carbon_dc (17.492 contra 18.755 gCO2e; "
    "p = 0,0020); bfd descartou 8,2% no Google; por VM atendida bfd cai do 8º ao 10º em "
    "go-realjun-alta e do 6º ao 10º em go-anticorr-alta")
m = expl[(expl["celula"] == "go-flat-folgada") & (expl["metrica"] == "total_gco2")
         & (((expl["alg_i"] == "ffd") & (expl["alg_j"] == "lowest_carbon_dc"))
            | ((expl["alg_i"] == "lowest_carbon_dc") & (expl["alg_j"] == "ffd")))]
r = m.iloc[0]
log(f"  go-flat-folgada {r['alg_i']}={num(r['media_i'],1)} vs {r['alg_j']}="
    f"{num(r['media_j'],1)} gCO2e; dif={pct(r['dif_pct'])} p_bruto={r['p_bruto']:.3e} "
    f"p_holm55={r['p_holm55']:.3e} significativo={bool(r['significativo'])}")
bfd = atend[(atend["algoritmo"] == "bfd") & atend["celula"].isin(CELULAS_22)].copy()
bfd["descarte_pct"] = 100 - bfd["atendimento_pct"]
google = bfd[bfd["celula"].str.contains("go")]
log(f"  bfd, descarte nas {len(google)} células Google das 22: "
    f"máximo={num(google['descarte_pct'].max(),2)}% "
    f"(em {google.loc[google['descarte_pct'].idxmax(), 'celula']}), "
    f"mediana={num(google['descarte_pct'].median(),2)}%")
for celula in ["go-realjun-alta", "go-anticorr-alta"]:
    r = atend[(atend["celula"] == celula) & (atend["algoritmo"] == "bfd")].iloc[0]
    log(f"  bfd em {celula}: descarte={num(100 - r['atendimento_pct'],2)}% "
        f"posto_total={num(r['posto_total'],1)} posto_por_vm={num(r['posto_por_vm'],1)} "
        f"difere={bool(r['posto_difere'])}")
n_dif = atend[atend["celula"].isin(CELULAS_22)]["posto_difere"].sum()
log(f"  combinações (célula × algoritmo) das 22 em que o posto muda ao normalizar "
    f"por VM atendida: {int(n_dif)}/{len(atend[atend['celula'].isin(CELULAS_22)])}")

# ---------------------------------------------------------------------------
log("\n\n== A-09 — vantagem sob sinal espacialmente variável e carga moderada ==")
log("antigo: 23,7% a 67,0% nas 4 configurações (p = 0,0020; r = -1,00)")
A09 = ["az-realjun-folgada", "az-anticorr-folgada", "go-realjun-folgada", "go-anticorr-folgada"]
for celula in A09:
    r = cab[cab["celula"] == celula].iloc[0]
    par = vantagem_ca_gb(celula, SEEDS_PAREADAS)
    log(f"  {celula}: vantagem={pct(r['vantagem_pct'])} [{r['melhores_ca']} x "
        f"{r['melhores_gb']}] p={r['p_bruto']:.3e} r={num(r['r_rank_biserial'])}")
    log(f"      recorte 42-51: vantagem={pct(par['vantagem_pct'])} "
        f"[{par['melhor_ca']} x {par['melhor_gb']}] p={par['p_bruto']:.3e}")
faixa = cab[cab["celula"].isin(A09)]["vantagem_pct"]
log(f"  faixa canônica: {num(faixa.min(),1)}% a {num(faixa.max(),1)}%")

# ---------------------------------------------------------------------------
log("\n\n== A-11 — menor kWh coincide com menor gCO2e em 9 de 12 ==")
log("antigo: 9/12; erros de proxy de 1,5%, 0,3% e 1,31x nos divergentes")
linhas11 = []
for celula in CELULAS_12:
    v_kwh = vc[(vc["celula"] == celula) & (vc["metrica"] == "total_kwh")].iloc[0]
    v_co2 = vc[(vc["celula"] == celula) & (vc["metrica"] == "total_gco2")].iloc[0]
    coincide = v_kwh["vencedores"] == v_co2["vencedores"]
    g = rk[(rk["celula"] == celula) & (rk["metrica"] == "total_gco2")]
    def media(alg):
        return float(g[g["algoritmo"] == alg]["media"].iloc[0])
    razao = np.nan
    if not coincide and v_kwh["n_vencedores"] == 1 and v_co2["n_vencedores"] == 1:
        razao = media(v_kwh["vencedores"]) / media(v_co2["vencedores"])
    linhas11.append({"celula": celula, "vencedor_kwh": v_kwh["vencedores"],
                     "vencedor_gco2": v_co2["vencedores"], "coincide": coincide,
                     "empate_kwh": bool(v_kwh["houve_empate_no_topo"]),
                     "empate_gco2": bool(v_co2["houve_empate_no_topo"]),
                     "razao_erro_proxy": razao,
                     "erro_proxy_pct": (razao - 1) * 100 if razao == razao else np.nan})
p11 = pd.DataFrame(linhas11)
p11.to_csv(os.path.join(OUT, "a11_proxy_12_referencia.csv"), index=False)
log(f"  coincidências: {int(p11['coincide'].sum())}/{len(p11)}")
for _, r in p11[~p11["coincide"]].iterrows():
    log(f"    {r['celula']}: kWh->{r['vencedor_kwh']} gCO2e->{r['vencedor_gco2']} "
        f"razão={num(r['razao_erro_proxy'],4)}× ({pct(r['erro_proxy_pct'])})")
log(f"  empates no topo em qualquer das duas métricas: "
    f"{int((p11['empate_kwh'] | p11['empate_gco2']).sum())}/{len(p11)}")

# ---------------------------------------------------------------------------
log("\n\n== A-14 — controle negativo flat ==")
log("antigo: tau = 1,000 nas 4 configurações; razão 475,0 exata em 440/440 execuções")
a14 = ler("a14_controle_flat.csv")
for _, r in a14.iterrows():
    log(f"  {r['celula']}: tau_b(gCO2e, kWh)={num(r['tau_b_gco2_kwh'],4)} "
        f"esperado={num(r['esperado'],4)} controle_ok={bool(r['controle_ok'])}")
flat = df[df["celula"].str.contains("-flat-")]
razao = flat["total_gco2"] / flat["total_kwh"]
log(f"  razão gCO2e/kWh nas células flat: {len(flat)} execuções; "
    f"mínimo={num(razao.min(),9)} máximo={num(razao.max(),9)}; "
    f"desvio máximo de 475,0 = {abs(razao - 475.0).max():.3e}")

# ---------------------------------------------------------------------------
log("\n\n== A-20 — correlação entre os rankings do Azure e do Google ==")
log("antigo: rho entre 0,855 e 0,991 (p <= 8,1e-4)")
a20 = ler("a20_azure_x_google.csv").copy()
a20["p_holm10"] = holm(a20["p_nao_corrigido"].to_numpy())
for _, r in a20.sort_values("rho_spearman").iterrows():
    log(f"  {r['celula_azure']} × {r['celula_google']}: rho={num(r['rho_spearman'],4)} "
        f"tau_b={num(r['tau_b'],4)} p={r['p_nao_corrigido']:.3e} "
        f"p_holm10={r['p_holm10']:.3e}")
log(f"  rho: mínimo={num(a20['rho_spearman'].min(),4)} máximo={num(a20['rho_spearman'].max(),4)} "
    f"mediana={num(a20['rho_spearman'].median(),4)}")
log(f"  significativos a 0,05 sem correção: {int((a20['p_nao_corrigido'] < 0.05).sum())}/{len(a20)}; "
    f"sob Holm m=10: {int((a20['p_holm10'] < 0.05).sum())}/{len(a20)}")
fora = a20[~a20["celula_azure"].str.contains("-flat-")]
log(f"  RECORTE DA AFIRMAÇÃO — fora das flat ({len(fora)} pares): "
    f"rho de {num(fora['rho_spearman'].min(),4)} a {num(fora['rho_spearman'].max(),4)}; "
    f"p máximo={fora['p_nao_corrigido'].max():.3e}; "
    f"p_holm máximo={fora['p_holm10'].max():.3e}")
dentro = a20[a20["celula_azure"].str.contains("-flat-")]
log(f"  contexto — pares flat, FORA do escopo da afirmação: "
    + "; ".join(f"{r['celula_azure']}={num(r['rho_spearman'],4)}" for _, r in dentro.iterrows()))
a20.to_csv(os.path.join(OUT, "a20_azure_x_google_holm.csv"), index=False)

# ---------------------------------------------------------------------------
log("\n\n== A-21 — pódio por célula ==")
log("antigo: cnemesis 1º em 3 moderadas e 3º sob alta; follow_renewables 3º->1º; "
    "pódio cnemesis 5, followme_s 3, follow_renewables 2; ffd 2º em geo3-go")
g = rk[(rk["metrica"] == "total_gco2") & rk["celula"].isin(CELULAS_22)]
primeiro, podio = {}, {}
for celula in CELULAS_22:
    s = g[g["celula"] == celula].sort_values("posto_medio")
    for a in s[s["vencedor"]]["algoritmo"]:
        primeiro[a] = primeiro.get(a, 0) + 1
    for a in s[s["posto_medio"] <= 3]["algoritmo"]:
        podio[a] = podio.get(a, 0) + 1
log("  primeiro colocado (empates contam para todos): "
    + "; ".join(f"{a}={n}" for a, n in sorted(primeiro.items(), key=lambda kv: -kv[1])))
log("  pódio (posto médio <= 3): "
    + "; ".join(f"{a}={n}" for a, n in sorted(podio.items(), key=lambda kv: -kv[1])))
for celula in CELULAS_22:
    s = g[g["celula"] == celula].sort_values("posto_medio").head(3)
    log(f"    {celula}: " + " | ".join(
        f"{r['algoritmo']} ({num(r['posto_medio'],1)})" for _, r in s.iterrows()))
folgadas = [c for c in CELULAS_12 if c.endswith("folgada")]
altas = [c for c in CELULAS_12 if c.endswith("alta")]
for rot, grupo in [("moderadas (12 ref.)", folgadas), ("altas (12 ref.)", altas)]:
    s = g[g["celula"].isin(grupo)]
    cn = s[s["algoritmo"] == "cnemesis"]
    log(f"  cnemesis nas {rot}: postos médios "
        + "; ".join(f"{r['celula']}={num(r['posto_medio'],1)}" for _, r in cn.iterrows()))
r = g[(g["celula"] == "geo3-go-realjun-folgada") & (g["algoritmo"] == "ffd")].iloc[0]
log(f"  ffd em geo3-go-realjun-folgada: posto_medio={num(r['posto_medio'],1)}")


# ---------------------------------------------------------------------------
log("\n\n== A-12 e A-13 — identidade de alocação e inércia de follow_renewables ==")
log("antigo A-12: identidade por construção, explicada a partir do código-fonte")
log("antigo A-13: 0 migrações; idêntico em 10/10 seeds")
aloc = []
for nome in sorted(os.listdir(os.path.join(HERE, "inputs"))):
    if nome.startswith("alocacao-") and nome.endswith(".tsv"):
        aloc.append(pd.read_csv(os.path.join(HERE, "inputs", nome),
                                sep="\t"))
al = pd.concat(aloc, ignore_index=True)
log(f"  cobertura da verificação em nível de evento: {len(al)} pares "
    f"(migrador x semente), {al['celula'].nunique()} células, "
    f"{al['migrador'].nunique()} migradores")
log(f"  pares com sequência de posicionamento idêntica: {int(al['identica'].sum())}/{len(al)}")
por_mig = al.groupby("migrador")["identica"].agg(["size", "sum"])
log("  por migrador: " + "; ".join(
    f"{m}={int(r['sum'])}/{int(r['size'])}" for m, r in por_mig.iterrows()))
div = al[al["identica"] == 0]
log(f"  onde diverge, diverge em massa: n_dst_diferentes de {int(div['n_dst_diferentes'].min())} "
    f"a {int(div['n_dst_diferentes'].max())} VMs; casos intermediários "
    f"(1 <= n_dst_diferentes <= 10): {int(((div['n_dst_diferentes'] >= 1) & (div['n_dst_diferentes'] <= 10)).sum())}")
sem_mig = al[al["n_migracoes"] == 0]
log(f"  execuções com ZERO migrações: {len(sem_mig)}; destas, com alocação idêntica: "
    f"{int(sem_mig['identica'].sum())} -> a identidade por construção vale exatamente "
    f"onde não há migração")
com_mig_id = al[(al["n_migracoes"] > 0) & (al["identica"] == 1)]
log(f"  execuções COM migração e ainda assim com alocação idêntica: {len(com_mig_id)} "
    f"(células: {sorted(com_mig_id['celula'].unique())})")
fr = al[al["migrador"] == "follow_renewables"]
g = fr.groupby("celula").agg(migracoes=("n_migracoes", "sum"),
                             identicas=("identica", "sum"), execucoes=("seed", "size"))
folg = g[g.index.str.contains("folgada")]
inertes = folg[folg["migracoes"] == 0]
log(f"  follow_renewables nas {len(folg)} células de carga moderada das 26: "
    f"inerte (0 migrações nas 10 sementes) em {len(inertes)}")
log("    exceções: " + "; ".join(
    f"{c}={int(r['migracoes'])} migrações em 10 sementes"
    for c, r in folg[folg["migracoes"] > 0].iterrows()))
fase1 = [c for c in g.index if not any(c.startswith(p) for p in ("geo3-", "hetcap-", "net100-", "pue-"))]
f1_folg = [c for c in fase1 if c.endswith("folgada")]
log(f"  recorte da fase 1 ({len(fase1)} células): moderadas={len(f1_folg)}, "
    f"todas inertes: {bool((g.loc[f1_folg, 'migracoes'] == 0).all())}; "
    f"alocação idêntica em 10/10 sementes: "
    f"{bool((g.loc[f1_folg, 'identicas'] == 10).all())}")
log(f"  células em que follow_renewables tem alocação idêntica nas 10 sementes: "
    f"{int((g['identicas'] == 10).sum())}/{len(g)}")

# ---------------------------------------------------------------------------
log("\n\n== A-22 — contagem de execuções e de configurações ==")
log("antigo: 3.890 execuções; 26 configurações executadas, 22 não redundantes; "
    "1.760 + 1.100 = 2.860 mais extensões")
for o in camp.origens:
    log("  " + o)
log(f"  campanha canônica: {len(df)} execuções, {len(camp.celulas)} configurações executadas")
redundantes = [c for c in camp.celulas if "realjan" in c]
log(f"  configurações redundantes por construção do dado (realjan): {len(redundantes)} -> "
    f"{len(camp.celulas) - len(redundantes)} não redundantes")
log(f"  soma conferida: " + " + ".join(str(len(q)) for q in
    [df[df['run_id'] == r] for r in sorted(df['run_id'].unique())])
    + f" = {len(df)}")
log(f"  células com n = 30: {sum(1 for c in camp.celulas if camp.n_seeds[c] == 30)}; "
    f"com n = 10: {sum(1 for c in camp.celulas if camp.n_seeds[c] == 10)}")

# ---------------------------------------------------------------------------
log("\n\n== A-23 — redundância das 4 configurações realjan ==")
log("antigo: 4/4 pares bit-idênticos")
# O tempo de decisão é medição de relógio, não resultado científico: varia entre
# execuções bit-idênticas conforme a carga da máquina. A redundância afirmada é
# das grandezas do modelo, e por isso as duas listas ficam separadas e são
# reportadas em separado; somá-las produziria "não idênticas" por um motivo que
# nada tem a ver com o dado de carbono.
TEMPORAIS = ["algo_wall_total_us"]
CIENTIFICAS = [c for c in df.columns
               if c not in ("celula", "algoritmo", "seed", "run_id", "gco2_por_vm")
               and c not in TEMPORAIS and pd.api.types.is_numeric_dtype(df[c])]
log(f"  colunas científicas comparadas ({len(CIENTIFICAS)}): {CIENTIFICAS}")
log(f"  colunas de medição de relógio, comparadas à parte ({len(TEMPORAIS)}): {TEMPORAIS}")
pares_ok = 0
for jan in sorted(redundantes):
    jun = jan.replace("realjan", "realjun")
    a = df[df["celula"] == jan].sort_values(["algoritmo", "seed"]).reset_index(drop=True)
    b = df[df["celula"] == jun].sort_values(["algoritmo", "seed"]).reset_index(drop=True)
    if len(a) != len(b):
        print(f"ABORTA: {jan} e {jun} com número de execuções diferente", file=sys.stderr)
        sys.exit(3)
    iguais = all(a[c].equals(b[c]) for c in CIENTIFICAS)
    pares_ok += int(iguais)
    dif_t = max(float((a[c] - b[c]).abs().max()) for c in TEMPORAIS)
    log(f"  {jan} x {jun}: {len(a)} execuções; colunas científicas idênticas: {iguais}; "
        f"desvio máximo no tempo de decisão = {dif_t:.0f} us")
log(f"  pares bit-idênticos nas grandezas do modelo: {pares_ok}/{len(redundantes)}")

# ---------------------------------------------------------------------------
caminho = os.path.join(OUT, "nivel3_extracoes.txt")
with open(caminho, "w", encoding="utf-8") as fh:
    fh.write("\n".join(digest) + "\n")
print(f"\ndigest -> {caminho}")

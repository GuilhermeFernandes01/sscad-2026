#!/usr/bin/env python3
"""A-17: capacidade heterogênea e saturação do data center limpo.

Fecha a afirmação A-17 do nível 3.

A afirmação tem quatro partes, e elas não se verificam do mesmo jeito:

  (i)   variação percentual de emissão por algoritmo, da célula-base para a
        célula hetcap, nos dois perfis de carbono (realjun e anticorr);
  (ii)  mudança de posto de `lowest_carbon_dc` (o número antigo diz 3 para 5);
  (iii) vantagem do melhor algoritmo sensível a carbono sobre o melhor
        empacotador genérico, antes e depois da capacidade heterogênea;
  (iv)  fração de alocações no data center mais limpo (paris), que é a
        explicação mecânica das outras três e só existe no nível de evento.

No cenário `hetcap` a topologia de nove data centers é mantida e a capacidade de
hospedeiros é redistribuída de modo que o data center mais limpo seja o menor:
paris tem 20 hospedeiros e johannesburg 80. O guloso de carbono, que sob
capacidade homogênea concentra no limpo, passa a esbarrar em capacidade e a
transbordar para os sujos. Sob `anticorr` a intensidade de carbono é
anticorrelacionada com o padrão diário, e o mesmo mecanismo pode favorecer.

Decisões, fixadas antes de ver o resultado:

1. Recorte pareado por semente, obrigatório. As células hetcap vêm da
   run-011 e têm 10 sementes; as células-base são da fase 1 e têm 30.
   Comparar 10 contra 30 mediria também o tamanho da amostra. As células-base
   são restritas às sementes 42-51, e o script aborta se o recorte não for
   exato. Mesma disciplina de A-04, A-16 e A-19.

2. Postos por midrank, não por `rank(method="min")`. O cálculo antigo usava
   min-rank, mas RE-2 proíbe que ordem de identificador ou de linha decida
   posição, e midrank é a regra fixada; `postos_medios` é a mesma função da
   reanálise principal, não uma reimplementação. As duas regras coincidem
   quando não há empate exato, e o digest declara se houve empate em cada
   célula; sem isso, a comparação "posto 3 para 5" não seria auditável.

3. Variação percentual sobre todos os 11 algoritmos, não sobre o subconjunto
   de sete do cálculo antigo. Restringir aqui só facilitaria o resultado; o
   subconjunto antigo é reportado à parte, para comparabilidade.

4. A vantagem só é reportada com topo único. Se o melhor sensível a carbono
   ou o melhor genérico empatar, não existe um contraste único e escolher um
   representante pelo nome seria desempate incidental, que a regra congelada
   proíbe. Nesse caso a vantagem sai como indefinida, que é resultado.

5. Duas famílias, ambas fixadas pelo desenho. A variação por algoritmo é
   testada por Wilcoxon pareado dentro de cada célula, com família de tamanho
   11 (os onze algoritmos, decididos pelo desenho e nunca pelos dados) e Holm
   dentro da célula. Os dois pares de células não se corrigem entre si: são
   perfis de carbono diferentes, respondendo a perguntas diferentes. Já a
   vantagem melhor-CA × melhor-GB é a mesma quantidade descritiva já reportada
   por célula na reanálise principal, com p bruto e sem família, sem inventar
   uma família de dois contrastes nem reduzi-la.

6. Fração de alocações no limpo agregada como média das frações por semente,
   não como razão dos totais somados. É a agregação do cálculo antigo, e
   trocá-la mudaria o número por mudança de definição, não por mudança de
   desempate.

7. Eventos lidos apenas das runs canônicas, pelo leitor compartilhado
   `eventos_canonicos.py`, que aborta se a mesma execução aparecer em mais de
   uma run. Nenhum outro traço pode embasar número publicável.

Uso:
  python3 analysis/hetcap_canonico.py [--parcial]

Saídas em `out/` (ou `out-parcial/`):
  a17_delta_gco2_por_algoritmo.csv
  a17_postos.csv
  a17_vantagem_ca_vs_gb.csv
  a17_alocacoes_no_limpo.csv
  a17_digest.txt
"""
import os
import sys

import numpy as np
import pandas as pd

from carga_canonica import CA, GB, ALGOS, carregar
from estatistica_comum import (holm, piso_wsr, postos_medios, rank_biserial,
                               vencedores, wsr_p)
from eventos_canonicos import dc_de, eventos

HERE = os.path.dirname(os.path.abspath(__file__))
PARCIAL = "--parcial" in sys.argv
DIR = os.path.join(HERE, "out-parcial" if PARCIAL else "out")
os.makedirs(DIR, exist_ok=True)

SEEDS_PAREADAS = sorted(range(42, 52))
CONJ_PAREADO = frozenset(SEEDS_PAREADAS)

# (célula hetcap, célula-base). O par é o mesmo do cálculo antigo.
PARES = [("hetcap-az-realjun-folgada", "az-realjun-folgada"),
         ("hetcap-az-anticorr-folgada", "az-anticorr-folgada")]

# Subconjunto de sete algoritmos do cálculo antigo, mantido apenas para
# comparabilidade (decisão 3 do cabeçalho).
ALGS_HET_ANTIGO = ["lowest_carbon_dc", "follow_renewables", "wsnb", "bfd",
                   "ffd", "first_fit", "cnemesis"]

DC_LIMPO = "paris"

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
    """Execuções da célula restritas às sementes 42-51, com verificação exata."""
    if celula not in campanha.celulas:
        abortar(f"célula ausente da campanha canônica: {celula}")
    sub = df[(df["celula"] == celula) & (df["seed"].isin(CONJ_PAREADO))]
    obtidas = frozenset(sub["seed"])
    if obtidas != CONJ_PAREADO:
        abortar(f"{celula}: recorte 42-51 incompleto, obtido {sorted(obtidas)}")
    return sub


log()
log("== A-17. Capacidade heterogênea (hetcap) e saturação do data center limpo ==")
log(f"  células hetcap: n = {', '.join(f'{h}={campanha.n_seeds[h]}' for h, _ in PARES)}")
log(f"  células-base:   n = {', '.join(f'{b}={campanha.n_seeds[b]}' for _, b in PARES)}"
    " -> restritas às sementes 42-51")

# --- (i) variação percentual de emissão por algoritmo ------------------------
reg = []
for c_het, c_base in PARES:
    het, base = recorte(c_het), recorte(c_base)
    for alg in ALGOS:
        a = het[het["algoritmo"] == alg].sort_values("seed")["total_gco2"].to_numpy()
        b = base[base["algoritmo"] == alg].sort_values("seed")["total_gco2"].to_numpy()
        if len(a) != len(SEEDS_PAREADAS) or len(b) != len(SEEDS_PAREADAS):
            abortar(f"{c_het}/{alg}: {len(a)} contra {len(b)} execuções no recorte")
        d = a - b
        reg.append({
            "celula_hetcap": c_het, "celula_base": c_base, "algoritmo": alg,
            "grupo": "carbon_aware" if alg in CA else "generico",
            "gco2_hetcap": float(a.mean()), "gco2_base": float(b.mean()),
            "delta_pct_vs_base": 100.0 * (a.mean() - b.mean()) / b.mean(),
            "p_bruto_wsr": wsr_p(d), "r_rank_biserial": rank_biserial(d),
            "no_subconjunto_antigo": alg in ALGS_HET_ANTIGO,
        })
delta = pd.DataFrame(reg)

# Holm dentro da célula, família de 11: tamanho do desenho, não dos dados (RE-2).
delta["p_holm11"] = np.nan
for c_het, _ in PARES:
    m = delta["celula_hetcap"] == c_het
    delta.loc[m, "p_holm11"] = holm(delta.loc[m, "p_bruto_wsr"].to_numpy())
delta["significativo"] = delta["p_holm11"] < 0.05

for c_het, c_base in PARES:
    sub = delta[delta["celula_hetcap"] == c_het].sort_values("delta_pct_vs_base")
    log()
    log(f"  -- (i) {c_het} contra {c_base}, variação de emissão --")
    log(f"     Wilcoxon pareado sobre 10 sementes; piso do teste exato = "
        f"{piso_wsr(len(SEEDS_PAREADAS)):.9f}; Holm com família de {len(ALGOS)}")
    for _, r in sub.iterrows():
        marca = " *" if r["no_subconjunto_antigo"] else "  "
        log(f"     {r['algoritmo']:18s} {r['grupo']:12s}{marca} "
            f"{r['gco2_base']:10.0f} -> {r['gco2_hetcap']:10.0f} gCO2e  "
            f"{r['delta_pct_vs_base']:+8.2f}%  p={r['p_bruto_wsr']:.4g}  "
            f"Holm11={r['p_holm11']:.4g}{'' if r['significativo'] else '  [NÃO SIGNIFICATIVO]'}")
    log(f"     significativos após Holm: {int(sub['significativo'].sum())}/{len(sub)}")
    ca_ = sub[sub["grupo"] == "carbon_aware"]["delta_pct_vs_base"]
    gb_ = sub[sub["grupo"] == "generico"]["delta_pct_vs_base"]
    log(f"     sensíveis a carbono: {ca_.min():+.2f}% a {ca_.max():+.2f}%")
    log(f"     genéricos:           {gb_.min():+.2f}% a {gb_.max():+.2f}%")
    log("     (* = algoritmo do subconjunto de sete do cálculo antigo)")

# --- (ii) postos ------------------------------------------------------------
reg = []
for c_het, c_base in PARES:
    for celula in (c_base, c_het):
        sub = recorte(celula)
        medias = {a: float(sub[sub["algoritmo"] == a]["total_gco2"].mean()) for a in ALGOS}
        postos = postos_medios(medias)
        empates = len(set(postos.values())) != len(postos)
        for alg in ALGOS:
            reg.append({
                "celula": celula, "papel": "base" if celula == c_base else "hetcap",
                "par": c_het, "algoritmo": alg, "gco2_medio": medias[alg],
                "posto_medio": float(postos[alg]),
                "ha_empate_de_posto_na_celula": empates,
            })
postos_df = pd.DataFrame(reg)

log()
log("  -- (ii) postos de emissão (midrank; menor = melhor) --")
for c_het, c_base in PARES:
    sub = postos_df[postos_df["par"] == c_het]
    for celula in (c_base, c_het):
        s = sub[sub["celula"] == celula].sort_values("posto_medio")
        emp = bool(s["ha_empate_de_posto_na_celula"].iloc[0])
        log(f"     {celula}: " + ", ".join(f"{r['algoritmo']}={r['posto_medio']:g}"
                                           for _, r in s.iterrows()))
        log(f"       empate exato de média entre algoritmos nesta célula: "
            f"{'SIM — midrank e min-rank divergem aqui' if emp else 'não — midrank e min-rank coincidem'}")
    for alg in ("lowest_carbon_dc", "follow_renewables", "wsnb"):
        pb = float(sub[(sub["celula"] == c_base) & (sub["algoritmo"] == alg)]["posto_medio"].iloc[0])
        ph = float(sub[(sub["celula"] == c_het) & (sub["algoritmo"] == alg)]["posto_medio"].iloc[0])
        log(f"       {alg}: posto {pb:g} -> {ph:g}")

# --- (iii) vantagem melhor sensível a carbono × melhor genérico --------------
reg = []
for c_het, c_base in PARES:
    for celula in (c_base, c_het):
        sub = recorte(celula)
        m_ca = {a: float(sub[sub["algoritmo"] == a]["total_gco2"].mean()) for a in CA}
        m_gb = {a: float(sub[sub["algoritmo"] == a]["total_gco2"].mean()) for a in GB}
        v_ca, v_gb = vencedores(postos_medios(m_ca)), vencedores(postos_medios(m_gb))
        if len(v_ca) == 1 and len(v_gb) == 1:
            x = sub[sub["algoritmo"] == v_ca[0]].sort_values("seed")["total_gco2"].to_numpy()
            y = sub[sub["algoritmo"] == v_gb[0]].sort_values("seed")["total_gco2"].to_numpy()
            d = x - y
            vant, p, r = 100.0 * (y.mean() - x.mean()) / y.mean(), wsr_p(d), rank_biserial(d)
        else:
            vant, p, r = np.nan, np.nan, np.nan
        reg.append({
            "celula": celula, "papel": "base" if celula == c_base else "hetcap",
            "par": c_het, "melhores_ca": "+".join(v_ca), "melhores_gb": "+".join(v_gb),
            "topo_unico": len(v_ca) == 1 and len(v_gb) == 1,
            "vantagem_pct": vant, "p_bruto_wsr": p, "r_rank_biserial": r,
        })
vant_df = pd.DataFrame(reg)

log()
log("  -- (iii) vantagem do melhor sensível a carbono sobre o melhor genérico --")
log("     (recorte de 10 sementes nos DOIS lados; a reanálise principal reporta")
log("      as células-base com 30 sementes, e os dois números não são intercambiáveis)")
for _, r in vant_df.iterrows():
    if r["topo_unico"]:
        log(f"     {r['celula']:30s} {r['melhores_ca']} vs {r['melhores_gb']}: "
            f"vantagem {r['vantagem_pct']:+.1f}%  p={r['p_bruto_wsr']:.4g}")
    else:
        log(f"     {r['celula']:30s} topo empatado "
            f"(CA={r['melhores_ca']}; GB={r['melhores_gb']}): vantagem indefinida")

principal = os.path.join(DIR, "melhor_ca_vs_melhor_gb.csv")
if os.path.isfile(principal):
    ref = pd.read_csv(principal).set_index("celula")
    log("     para contraste, o mesmo par com o n da célula na reanálise principal:")
    for _, r in vant_df.iterrows():
        c = r["celula"]
        if c in ref.index:
            log(f"       {c:30s} n={campanha.n_seeds[c]:2d}: "
                f"{ref.loc[c, 'vantagem_pct']:+.1f}%")

# --- (iv) fração de alocações no data center limpo ---------------------------
log()
log(f"  -- (iv) fração de alocações em {DC_LIMPO}, por evento --")
log("     lida de decisions.jsonl (kind == 'placement'), média das frações por semente")
reg = []
for c_het, c_base in PARES:
    for celula in (c_base, c_het):
        for alg in ALGOS:
            fracs, totais, no_limpo = [], [], []
            for seed in SEEDS_PAREADAS:
                por_dc = {}
                for ev in eventos(celula, alg, seed):
                    if ev["kind"] == "placement":
                        d = dc_de(ev["dst"])
                        por_dc[d] = por_dc.get(d, 0) + 1
                tot = sum(por_dc.values())
                if tot == 0:
                    abortar(f"{celula}/{alg}/seed_{seed}: nenhuma alocação no traço")
                totais.append(tot)
                no_limpo.append(por_dc.get(DC_LIMPO, 0))
                fracs.append(por_dc.get(DC_LIMPO, 0) / tot)
            reg.append({
                "celula": celula, "papel": "base" if celula == c_base else "hetcap",
                "par": c_het, "algoritmo": alg,
                "alocacoes_media": float(np.mean(totais)),
                "alocacoes_no_limpo_media": float(np.mean(no_limpo)),
                "frac_limpo_media": float(np.mean(fracs)),
                "no_subconjunto_antigo": alg in ALGS_HET_ANTIGO,
            })
aloc = pd.DataFrame(reg)

for c_het, c_base in PARES:
    sub = aloc[aloc["par"] == c_het]
    log()
    log(f"     {c_base} -> {c_het}")
    for alg in ALGOS:
        fb = float(sub[(sub["celula"] == c_base) & (sub["algoritmo"] == alg)]["frac_limpo_media"].iloc[0])
        fh = float(sub[(sub["celula"] == c_het) & (sub["algoritmo"] == alg)]["frac_limpo_media"].iloc[0])
        marca = " *" if alg in ALGS_HET_ANTIGO else "  "
        log(f"       {alg:18s}{marca} {100*fb:6.2f}% -> {100*fh:6.2f}%  "
            f"({fh - fb:+.4f} em fração)")

delta.to_csv(os.path.join(DIR, "a17_delta_gco2_por_algoritmo.csv"), index=False)
postos_df.to_csv(os.path.join(DIR, "a17_postos.csv"), index=False)
vant_df.to_csv(os.path.join(DIR, "a17_vantagem_ca_vs_gb.csv"), index=False)
aloc.to_csv(os.path.join(DIR, "a17_alocacoes_no_limpo.csv"), index=False)
log()
log("== artefatos escritos ==")
for nome in ("a17_delta_gco2_por_algoritmo.csv", "a17_postos.csv",
             "a17_vantagem_ca_vs_gb.csv", "a17_alocacoes_no_limpo.csv"):
    log(f"  {nome}")

with open(os.path.join(DIR, "a17_digest.txt"), "w") as fh:
    fh.write("\n".join(linhas) + "\n")
sys.exit(0)

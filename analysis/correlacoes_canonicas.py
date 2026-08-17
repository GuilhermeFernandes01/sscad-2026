#!/usr/bin/env python3
"""Correlações de ranking sobre os dados canônicos.

Fecha quatro das onze afirmações do nível 3 que a reanálise canônica principal
não cobre:

  A-07  estabilidade do ranking entre configurações (τ_b par a par entre células)
  A-10  τ_b entre ranking por gCO2e e por kWh, com e sem PUE invertido
  A-14  controle negativo flat: τ_b entre gCO2e e kWh deve ser 1,000
  A-20  ρ de Spearman entre as células azure e as google correspondentes

Todas leem o mesmo insumo, `out/rankings_postos_medios.csv`, produzido pela
reanálise canônica, e nenhuma re-executa simulação: é análise sobre resultado já
registrado, não experimento novo.

Ficou em script separado porque a reanálise canônica é o artefato que decide
significância e está congelada em torno das regras de RE-2 (famílias fixas, p=1
para degenerado, midrank, τ_b). Acrescentar seções a ela a cada afirmação
pendente aumentaria a superfície de um artefato já auditado. Correlação de
ranking é análise derivada: consome a saída, não os dados brutos, e pode falhar
sem invalidar nada acima dela.

Decisões estatísticas, fixadas antes de ver o resultado:

1. τ_b, não τ_a nem τ_c, decisão fixada sob RE-2. Os rankings têm empates, e a
   política de desempate subespecificada é justamente o objeto desta campanha;
   τ_a ignora empates e infla a concordância aparente, enquanto τ_b corrige
   pelo número de pares empatados nos dois vetores.

2. Postos médios (midrank), não posto ordinal, também RE-2. Com posto ordinal a
   ordem de linha decidiria quem fica na frente num empate, que é o defeito que
   a campanha canônica existe para eliminar.

3. Sem correção de múltiplas comparações nas correlações, que são descritivas:
   nenhuma afirmação do artigo depende de "τ significativamente diferente de
   zero". Os p-valores são reportados porque estão disponíveis, e ficam marcados
   como não corrigidos. Aplicar Holm aqui sugeriria um teste confirmatório que
   não existe.

4. A-14 é controle negativo, com limiar declarado. Sob perfil flat a intensidade
   de carbono é constante, gCO2e é função afim de kWh, e os dois rankings têm de
   coincidir em τ_b = 1,000. Qualquer valor diferente de 1,0 nas quatro células
   flat é defeito, não achado, e o script marca a célula como FALHA.

5. A-20 pareia célula a célula pelo nome. `az-realjun-alta` com
   `go-realjun-alta`, e assim por diante. Células sem par (as de fase 2-3
   exclusivas do Azure, como `hetcap-*` e `net100-az-*`) ficam de fora e são
   listadas explicitamente, para que a ausência não passe por omissão.

Uso (da raiz do artefato):
  python3 analysis/correlacoes_canonicas.py [--parcial]

Sem `--parcial` lê `out/` e escreve em `out/`; com `--parcial`, lê e escreve em
`out-parcial/`; mesma convenção da reanálise principal, e pelo mesmo motivo:
`out-parcial/` não é citável.

Código de saída: 0 se tudo correr; 1 se o controle negativo da A-14 falhar ou se
o insumo não existir.
"""
import itertools
import os
import sys

import pandas as pd
from scipy import stats

HERE = os.path.dirname(os.path.abspath(__file__))
PARCIAL = "--parcial" in sys.argv
DIR = os.path.join(HERE, "out-parcial" if PARCIAL else "out")
ENTRADA = os.path.join(DIR, "rankings_postos_medios.csv")

# Sob perfil flat, gCO2e = 475,0 x kWh exatamente
CELULAS_FLAT = ["az-flat-alta", "az-flat-folgada", "go-flat-alta", "go-flat-folgada"]
TAU_ESPERADO_FLAT = 1.0
TOL_FLAT = 1e-9

falhas = []
linhas_saida = []


def log(msg=""):
    print(msg)
    linhas_saida.append(msg)


def tau_b(x, y):
    """τ_b de Kendall. Retorna (tau, p). Variante 'b' é o padrão do scipy."""
    res = stats.kendalltau(x, y, variant="b")
    return float(res.statistic), float(res.pvalue)


if not os.path.exists(ENTRADA):
    print(f"ABORTADO: {ENTRADA} não existe. Rode antes a reanálise canônica"
          f"{' com --parcial' if PARCIAL else ' sem --parcial'}.", file=sys.stderr)
    sys.exit(1)

rk = pd.read_csv(ENTRADA)
if PARCIAL:
    log("AVISO: execução --parcial. Nada aqui é citável no artigo.")
log(f"== insumo: {os.path.relpath(ENTRADA, HERE)} "
    f"({len(rk)} linhas, {rk['celula'].nunique()} células, "
    f"{rk['metrica'].nunique()} métricas, {rk['algoritmo'].nunique()} algoritmos) ==")

ALGORITMOS = sorted(rk["algoritmo"].unique())
CELULAS = sorted(rk["celula"].unique())


def vetor(celula, metrica):
    """Postos médios na ordem fixa de ALGORITMOS. É a ordem fixa que torna dois
    vetores comparáveis; ordenar por posto faria a comparação perder sentido."""
    sub = rk[(rk["celula"] == celula) & (rk["metrica"] == metrica)]
    m = dict(zip(sub["algoritmo"], sub["posto_medio"]))
    faltando = [a for a in ALGORITMOS if a not in m]
    if faltando:
        raise SystemExit(f"erro: {celula}/{metrica} sem postos para {faltando}")
    return [m[a] for a in ALGORITMOS]


# --- A-14: controle negativo flat -------------------------------------------
log()
log("== A-14. Controle negativo flat: τ_b(gCO2e, kWh) deve ser exatamente 1,000 ==")
log("  Sob perfil flat a intensidade é constante; os dois rankings são o mesmo")
log("  ranking. Desvio aqui é defeito do pipeline, não resultado.")
flat_rows = []
for c in CELULAS_FLAT:
    if c not in CELULAS:
        falhas.append(f"A-14: célula flat ausente do insumo: {c}")
        continue
    t, p = tau_b(vetor(c, "total_gco2"), vetor(c, "total_kwh"))
    ok = abs(t - TAU_ESPERADO_FLAT) <= TOL_FLAT
    flat_rows.append({"celula": c, "tau_b_gco2_kwh": t, "p_nao_corrigido": p,
                      "esperado": TAU_ESPERADO_FLAT, "controle_ok": ok})
    log(f"  {c:22s} τ_b = {t:.6f}  {'OK' if ok else 'FALHA'}")
    if not ok:
        falhas.append(f"A-14: {c} τ_b = {t:.6f}, esperado {TAU_ESPERADO_FLAT}")

# --- A-10: gCO2e x kWh em todas as células, com destaque para PUE -----------
log()
log("== A-10. τ_b entre ranking por gCO2e e ranking por kWh, por célula ==")
log("  A afirmação do artigo é que sob PUE invertido a correlação desaba, ou seja,")
log("  que minimizar energia deixa de ser proxy de minimizar carbono.")
prox_rows = []
for c in CELULAS:
    t, p = tau_b(vetor(c, "total_gco2"), vetor(c, "total_kwh"))
    prox_rows.append({"celula": c, "tau_b_gco2_kwh": t, "p_nao_corrigido": p,
                      "eh_pue": c.startswith("pue-"), "eh_flat": c in CELULAS_FLAT})
prox = pd.DataFrame(prox_rows).sort_values("tau_b_gco2_kwh")
for _, r in prox.iterrows():
    marca = " [PUE]" if r["eh_pue"] else (" [flat]" if r["eh_flat"] else "")
    log(f"  {r['celula']:26s} τ_b = {r['tau_b_gco2_kwh']:+.4f}{marca}")

# --- A-07: estabilidade do ranking entre configurações ----------------------
log()
log("== A-07. Estabilidade do ranking de gCO2e entre pares de células (τ_b) ==")
log("  Um τ mediano alto com mínimo baixo significa ranking estável na maioria")
log("  das configurações e instável em algumas — que é uma afirmação diferente,")
log("  e mais fraca, do que 'o ranking é estável'.")
pares = []
for a, b in itertools.combinations(CELULAS, 2):
    t, p = tau_b(vetor(a, "total_gco2"), vetor(b, "total_gco2"))
    pares.append({"celula_i": a, "celula_j": b, "tau_b": t, "p_nao_corrigido": p})
est = pd.DataFrame(pares)
log(f"  {len(est)} pares de células")
log(f"  τ_b mediana = {est['tau_b'].median():.4f}; "
    f"mínimo = {est['tau_b'].min():.4f}; máximo = {est['tau_b'].max():.4f}; "
    f"1o quartil = {est['tau_b'].quantile(0.25):.4f}")
log("  cinco pares menos concordantes:")
for _, r in est.nsmallest(5, "tau_b").iterrows():
    log(f"    {r['celula_i']:26s} x {r['celula_j']:26s} τ_b = {r['tau_b']:+.4f}")

# --- A-20: robustez ao workload (azure x google) ----------------------------
log()
log("== A-20. ρ de Spearman entre células azure e google correspondentes ==")
pares_wl, sem_par = [], []
for c in CELULAS:
    if not c.startswith("az-"):
        continue
    par = "go-" + c[len("az-"):]
    if par not in CELULAS:
        sem_par.append(c)
        continue
    rho, p = stats.spearmanr(vetor(c, "total_gco2"), vetor(par, "total_gco2"))
    t, pt = tau_b(vetor(c, "total_gco2"), vetor(par, "total_gco2"))
    pares_wl.append({"celula_azure": c, "celula_google": par,
                     "rho_spearman": float(rho), "p_nao_corrigido": float(p),
                     "tau_b": t})
wl = pd.DataFrame(pares_wl).sort_values("rho_spearman")
for _, r in wl.iterrows():
    log(f"  {r['celula_azure']:22s} x {r['celula_google']:22s} "
        f"ρ = {r['rho_spearman']:+.4f}  τ_b = {r['tau_b']:+.4f}")
outras = [c for c in CELULAS if not c.startswith("az-") and not c.startswith("go-")]
log(f"  células azure sem par google ({len(sem_par)}): "
    f"{', '.join(sem_par) if sem_par else 'nenhuma'}")
log(f"  células fora do pareamento az/go ({len(outras)}): "
    f"{', '.join(outras) if outras else 'nenhuma'}")
log("  Estas ficam FORA da A-20 por construção, não por seleção de resultado.")

# --- saída ------------------------------------------------------------------
pd.DataFrame(flat_rows).to_csv(os.path.join(DIR, "a14_controle_flat.csv"), index=False)
prox.to_csv(os.path.join(DIR, "a10_proxy_kwh_gco2.csv"), index=False)
est.to_csv(os.path.join(DIR, "a07_estabilidade_ranking.csv"), index=False)
wl.to_csv(os.path.join(DIR, "a20_azure_x_google.csv"), index=False)

log()
log("== artefatos escritos ==")
for nome in ("a14_controle_flat.csv", "a10_proxy_kwh_gco2.csv",
             "a07_estabilidade_ranking.csv", "a20_azure_x_google.csv"):
    log(f"  {nome}")
log()
log("Todos os p-valores acima são NÃO corrigidos e descritivos: nenhuma afirmação")
log("do artigo depende de significância de correlação. Ver decisão 3 no cabeçalho.")

with open(os.path.join(DIR, "correlacoes_digest.txt"), "w") as fh:
    fh.write("\n".join(linhas_saida) + "\n")

if falhas:
    print()
    print(f"REPROVADO: {len(falhas)} falha(s)")
    for f in falhas:
        print(f"  {f}")
    sys.exit(1)
sys.exit(0)

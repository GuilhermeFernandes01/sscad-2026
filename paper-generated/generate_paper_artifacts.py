#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Gera tabelas, figuras e snippets do artigo a partir apenas de
`../canonical-results/`.

O que este gerador não pode fazer, por regra fixada a priori: escolher melhor
algoritmo; decidir classe A/B; escolher normalizador; decidir significância;
refazer Holm; recalcular BCa; variar arredondamento por figura; consultar
qualquer `runs/run-*`; consultar a matriz provisória; consumir números do
main.tex antigo.

O que ele pode: selecionar valores já congelados; ordenar para apresentação;
arredondar segundo a política fixada abaixo; formatar; montar tabelas;
renderizar gráficos.

Guarda de legalidade: nenhuma célula da classe A pode aparecer em representação
de vencedor ou ranking. Se uma tabela ou figura tentar, o build falha.

Política de arredondamento, única para todo artefato:
  - percentuais: 2 casas decimais;
  - postos: inteiro quando íntegro, senão 1 casa decimal.

Determinismo: SOURCE_DATE_EPOCH=0 e ordenações totais em toda saída.

Uso: python3 generate_paper_artifacts.py [--outdir DIR]  (default: ao lado)
"""
import argparse
import csv
import hashlib
import json
import os
import sys
from pathlib import Path

os.environ["SOURCE_DATE_EPOCH"] = "0"

import matplotlib
matplotlib.use("Agg")
matplotlib.rcParams["pdf.fonttype"] = 42
import matplotlib.pyplot as plt

AQUI = Path(__file__).resolve().parent
CANON = AQUI.parent / "canonical-results"

ARREDONDA_PCT = 2   # casas decimais de percentual
ARREDONDA_POSTO = 1

ALGO_LABEL = {
    "best_fit": "best\\_fit", "bfd": "bfd", "cnemesis": "cnemesis",
    "ffd": "ffd", "first_fit": "first\\_fit",
    "follow_renewables": "follow\\_renewables", "followme_s": "followme\\_s",
    "lowest_carbon_dc": "lowest\\_carbon\\_dc", "round_robin": "round\\_robin",
    "worst_fit": "worst\\_fit", "wsnb": "wsnb",
}

# Guarda de unidade: strings que não podem aparecer em nenhum artefato gerado,
# dadas as confusões documentadas (BCa kg/g; MiB/GiB; emitidas/concluídas).
UNIDADES_PROIBIDAS = ["kgCO", "GiB", "migrações concluídas", "migracoes concluidas"]


def le_canonico(rel: str) -> Path:
    """Único ponto de leitura, para que toda entrada venha do pacote congelado."""
    p = (CANON / rel).resolve()
    if CANON.resolve() not in p.parents and p != CANON.resolve():
        raise SystemExit(f"ABORTA: leitura fora de canonical-results/: {p}")
    if not p.is_file():
        raise SystemExit(f"ABORTA: entrada canônica ausente: {p}")
    return p


def csv_canonico(rel: str):
    with le_canonico(rel).open(encoding="utf-8") as fh:
        return list(csv.DictReader(fh))


def pct(x: float) -> str:
    return f"{x:.{ARREDONDA_PCT}f}"


def posto(x: float) -> str:
    return str(int(x)) if float(x).is_integer() else f"{x:.{ARREDONDA_POSTO}f}"


def escreve(caminho: Path, texto: str, registro: list, fontes: list, transf: str):
    for probe in UNIDADES_PROIBIDAS:
        if probe in texto:
            raise SystemExit(f"ABORTA (guarda de unidade): '{probe}' em {caminho.name}")
    caminho.parent.mkdir(parents=True, exist_ok=True)
    caminho.write_text(texto, encoding="utf-8")
    registro.append({"saida": str(caminho.relative_to(caminho.parents[1])),
                     "fontes": fontes, "transformacao": transf})


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--outdir", default=str(AQUI))
    out = Path(ap.parse_args().outdir).resolve()
    registro = []

    prov_canon = json.loads(le_canonico("provenance.json").read_text())
    classe_a = set(prov_canon["celulas"]["classe_a_comparacao_escalar_proibida"])
    classe_b = set(prov_canon["celulas"]["classe_b_ordenacao_robusta_reportar_atendimento"])
    integrais = set(prov_canon["celulas"]["atendimento_integral"])
    comparaveis = sorted(integrais) + sorted(classe_b)

    # ---- T1: as 30 células e suas classes -----------------------------------
    linhas = ["% GERADO por generate_paper_artifacts.py - NÃO EDITAR",
              "% fonte: canonical-results/provenance.json#celulas",
              "\\begin{tabular}{ll}", "\\toprule",
              "Célula & Classe \\\\", "\\midrule"]
    rotulo = {"integral": "atendimento integral",
              "B": "trab.\\ desigual, ordenação robusta (B)",
              "A": "comparação escalar proibida (A)"}
    for cel in sorted(integrais) + sorted(classe_b) + sorted(classe_a):
        cls = "integral" if cel in integrais else ("B" if cel in classe_b else "A")
        linhas.append(f"{cel.replace('_', '\\_')} & {rotulo[cls]} \\\\")
    linhas += ["\\bottomrule", "\\end{tabular}", ""]
    escreve(out / "tables" / "celulas-classes.tex", "\n".join(linhas), registro,
            ["provenance.json#celulas"], "transcrição das listas congeladas; ordenação alfabética por classe")

    # ---- T2 + F1: vencedores por célula, só células comparáveis -------------
    venc = {r["celula"]: r for r in csv_canonico("analytical-outputs/vencedores_por_celula_metrica.csv")
            if r["metrica"] == "total_gco2"}
    atend = {}
    for r in csv_canonico("analytical-outputs/atendimento_e_postos.csv"):
        c = r["celula"]
        atend.setdefault(c, []).append(float(r["atendimento_pct"]))

    # Guarda de legalidade: o conjunto apresentado como "vencedor" não pode
    # tocar a classe A.
    for cel in comparaveis:
        if cel in classe_a:
            raise SystemExit(f"ABORTA (legalidade): célula classe A em contexto de vencedor: {cel}")
    faltando = [c for c in comparaveis if c not in venc]
    if faltando:
        raise SystemExit(f"ABORTA: células comparáveis sem vencedor congelado: {faltando}")

    linhas = ["% GERADO por generate_paper_artifacts.py - NÃO EDITAR",
              "% fonte: canonical-results/analytical-outputs/vencedores_por_celula_metrica.csv",
              "%        (metrica=total_gco2), restrito às 18 células comparáveis;",
              "%        atendimento de canonical-results/analytical-outputs/atendimento_e_postos.csv",
              "% As 12 células da classe A NÃO aparecem: comparação escalar proibida.",
              "\\begin{tabular}{llrl}", "\\toprule",
              "Célula & Vencedor (gCO$_2$e total) & Atend.\\ mín.\\ (\\%) & Grupo \\\\",
              "\\midrule"]
    fig_dados = []
    for cel in comparaveis:
        r = venc[cel]
        nomes = " ; ".join(ALGO_LABEL[a] for a in sorted(r["vencedores"].split(";")))
        if r["houve_empate_no_topo"] == "True":
            nomes += " (empate)"
        amin = min(atend[cel])
        grupo = "integral" if cel in integrais else "B"
        # célula classe B: atendimento sempre reportado junto (regra congelada)
        atxt = pct(amin)
        linhas.append(f"{cel.replace('_', '\\_')} & {nomes} & {atxt} & {grupo} \\\\")
        fig_dados.append((cel, sorted(r["vencedores"].split(";")), amin, grupo))
    linhas += ["\\bottomrule", "\\end{tabular}", ""]
    escreve(out / "tables" / "vencedores-comparaveis.tex", "\n".join(linhas), registro,
            ["analytical-outputs/vencedores_por_celula_metrica.csv",
             "analytical-outputs/atendimento_e_postos.csv", "provenance.json#celulas"],
            "seleção (metrica=total_gco2; 18 células comparáveis) + mínimo de coluna por célula + formatação")

    # F1: mapa categórico de vencedores nas 18 células comparáveis
    algos_presentes = sorted({a for _, vs, _, _ in fig_dados for a in vs})
    fig, ax = plt.subplots(figsize=(7.0, 5.4))
    ycels = [c for c, _, _, _ in fig_dados][::-1]
    for yi, cel in enumerate(ycels):
        _, vs, amin, grupo = fig_dados[len(fig_dados) - 1 - yi]
        for a in vs:
            xi = algos_presentes.index(a)
            ax.scatter(xi, yi, marker="s", s=90,
                       color="#2b6cb0" if grupo == "integral" else "#c05621")
        ax.text(len(algos_presentes) - 0.35, yi, f"{pct(amin)}%",
                va="center", fontsize=7, color="#555555")
    ax.set_xticks(range(len(algos_presentes)))
    ax.set_xticklabels([a.replace("_", "\n") for a in algos_presentes], fontsize=7)
    ax.set_yticks(range(len(ycels)))
    ax.set_yticklabels(ycels, fontsize=7)
    ax.set_xlim(-0.5, len(algos_presentes) + 0.4)
    ax.set_title("Vencedor por célula: emissão total (gCO$_2$e), células comparáveis\n"
                 "azul: atendimento integral; laranja: classe B (atend. mín. anotado)",
                 fontsize=9)
    ax.set_xlabel("algoritmo vencedor (empates: múltiplas marcas)", fontsize=8)
    fig.tight_layout()
    figpath = out / "figures" / "fig-vencedores-comparaveis.pdf"
    figpath.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(figpath, metadata={"CreationDate": None}, bbox_inches="tight", pad_inches=0.03)
    plt.close(fig)
    registro.append({"saida": "figures/fig-vencedores-comparaveis.pdf",
                     "fontes": ["analytical-outputs/vencedores_por_celula_metrica.csv",
                                "analytical-outputs/atendimento_e_postos.csv",
                                "provenance.json#celulas"],
                     "transformacao": "mesmos dados de tables/vencedores-comparaveis.tex, renderizados"})

    # ---- F2: classe A, postos sob três normalizadores -----------------------
    # Deriva postos ordenando colunas congeladas (transformação de apresentação)
    # e valida contra os números congelados de comparacao_normalizadores.csv
    # (postos_diferem_* e vencedor_*). Não é ranking do artigo: é a demonstração
    # de que normalizadores plausíveis produzem ordenações incompatíveis
    # (R-TADM-1). Sem declarar vencedor.
    VITRINE = ["go-flat-alta", "geo3-go-realjun-folgada"]
    NORM = [("total_gco2", "N0: emissão total"),
            ("gco2_por_vm", "N1: emissão/VM alocada"),
            ("gco2_por_mips", "N2: emissão/demanda admitida")]
    medias = {}
    for r in csv_canonico("normalizadores/medias_por_celula_algoritmo.csv"):
        medias.setdefault(r["celula"], {})[r["algoritmo"]] = r
    comp = {r["celula"]: r for r in csv_canonico("normalizadores/comparacao_normalizadores.csv")}

    def postos_de(cel, col):
        vals = sorted(medias[cel].items(), key=lambda kv: (float(kv[1][col]), kv[0]))
        return {a: i + 1 for i, (a, _) in enumerate(vals)}

    # Sem título interno (a legenda do float carrega a mensagem) e altura
    # reduzida para recuperar espaço vertical no artigo
    fig, axes = plt.subplots(1, len(VITRINE), figsize=(6.8, 2.35), sharey=True)
    for ax, cel in zip(axes, VITRINE):
        if cel not in classe_a:
            raise SystemExit(f"ABORTA: célula de vitrine fora da classe A: {cel}")
        ranks = {c: postos_de(cel, c) for c, _ in NORM}
        # validação contra os congelados
        pares = [("total_gco2", "gco2_por_vm", "postos_diferem_N0_N1"),
                 ("total_gco2", "gco2_por_mips", "postos_diferem_N0_N2"),
                 ("gco2_por_vm", "gco2_por_mips", "postos_diferem_N1_N2")]
        for a_, b_, chave in pares:
            dif = sum(1 for alg in ranks[a_] if ranks[a_][alg] != ranks[b_][alg])
            if dif != int(comp[cel][chave]):
                raise SystemExit(f"ABORTA: {cel} {chave}: derivado {dif} != congelado {comp[cel][chave]}")
        for col, vkey in [("total_gco2", "vencedor_N0"), ("gco2_por_vm", "vencedor_N1"),
                          ("gco2_por_mips", "vencedor_N2")]:
            melhor = min(ranks[col], key=lambda a: ranks[col][a])
            if melhor != comp[cel][vkey]:
                raise SystemExit(f"ABORTA: {cel} {vkey}: derivado {melhor} != congelado {comp[cel][vkey]}")
        algos = sorted(medias[cel])
        marcadores = ["o", "s", "^", "v", "D", "P", "X", "*", "<", ">", "h"]
        for k, alg in enumerate(algos):
            ys = [ranks[c][alg] for c, _ in NORM]
            ax.plot(range(3), ys, marker=marcadores[k % len(marcadores)],
                    ms=3.5, lw=1.1, alpha=0.85)
            ax.text(2.06, ys[2], alg.replace("_", "\\_") if False else alg,
                    fontsize=6.5, va="center")
        ax.set_xticks(range(3))
        ax.set_xticklabels([lbl for _, lbl in NORM], fontsize=7, rotation=12)
        rotulos_vitrine = {"go-flat-alta": "Google · flat · carga alta",
                           "geo3-go-realjun-folgada": "Google · geo3 · sintético de junho · carga moderada"}
        ax.set_title(rotulos_vitrine[cel], fontsize=8)
        ax.invert_yaxis()
        ax.set_xlim(-0.2, 3.15)
    axes[0].set_ylabel("posto (1 = menor emissão sob a métrica)", fontsize=8)
    fig.tight_layout()
    f2 = out / "figures" / "fig-classeA-normalizadores.pdf"
    fig.savefig(f2, metadata={"CreationDate": None}, bbox_inches="tight", pad_inches=0.03)
    plt.close(fig)
    registro.append({"saida": "figures/fig-classeA-normalizadores.pdf",
                     "fontes": ["normalizadores/medias_por_celula_algoritmo.csv",
                                "normalizadores/comparacao_normalizadores.csv",
                                "provenance.json#celulas"],
                     "transformacao": "postos por ordenação de colunas congeladas, validados contra postos_diferem_* e vencedor_* congelados; sem declaração de vencedor (R-TADM-1)"})

    # ---- F3: sensibilidade ao cenário, 5 painéis, métricas naturais ---------
    # Cada painel preserva a métrica natural da análise; não há escala comum.
    # Todos os valores são selecionados dos outputs congelados e validados
    # aqui; divergência aborta o build.
    def _val(rel, filtro, campo):
        for r in csv_canonico(rel):
            if all(r[k] == v for k, v in filtro.items()):
                return float(r[campo])
        raise SystemExit(f"ABORTA: valor não encontrado em {rel}: {filtro}")

    pue_ratio = _val("analytical-outputs/a10_razao_erro_proxy.csv",
                     {"celula": "pue-az-realjun-folgada", "papel": "pue_invertido"},
                     "razao_erro_proxy")
    if abs(pue_ratio - 3.2462606030720047) > 1e-9:
        raise SystemExit(f"ABORTA: razão PUE {pue_ratio} != valor canônico")
    rede = {a: _val("analytical-outputs/tost_equivalencia_1pct.csv",
                    {"celula": "net100-az-realjun-alta", "algoritmo": a}, "dif_pct")
            for a in ("cnemesis", "follow_renewables", "followme_s")}
    for a, esp in [("cnemesis", -0.02655493609853381), ("follow_renewables", -0.09659051342664915),
                   ("followme_s", -0.1438701621411615)]:
        if abs(rede[a] - esp) > 1e-9:
            raise SystemExit(f"ABORTA: delta rede {a} difere")
    geo3 = {
        "Azure": (_val("analytical-outputs/melhor_ca_vs_melhor_gb.csv",
                       {"celula": "az-realjun-folgada"}, "vantagem_pct"),
                  _val("analytical-outputs/a06_geo3_recorte.csv",
                       {"celula": "geo3-az-realjun-folgada"}, "vantagem_pct")),
        "Google": (_val("analytical-outputs/melhor_ca_vs_melhor_gb.csv",
                        {"celula": "go-realjun-folgada"}, "vantagem_pct"),
                   _val("analytical-outputs/a06_geo3_recorte.csv",
                        {"celula": "geo3-go-realjun-folgada"}, "vantagem_pct")),
    }
    if not (58.7 < geo3["Azure"][0] < 58.8 and 2.8 < geo3["Azure"][1] < 3.0
            and 65.9 < geo3["Google"][0] < 66.1 and 6.3 < geo3["Google"][1] < 6.5):
        raise SystemExit("ABORTA: valores geo3 divergem dos canônicos")
    het_lcdc = _val("analytical-outputs/a17_delta_gco2_por_algoritmo.csv",
                    {"celula_hetcap": "hetcap-az-realjun-folgada", "algoritmo": "lowest_carbon_dc"},
                    "delta_pct_vs_base")
    het_gen = [float(r["delta_pct_vs_base"]) for r in
               csv_canonico("analytical-outputs/a17_delta_gco2_por_algoritmo.csv")
               if r["celula_hetcap"] == "hetcap-az-realjun-folgada" and r["grupo"] == "generico"]
    if abs(het_lcdc - 101.54034305) > 1e-3 or not (11.8 < min(het_gen) < 11.9 and 23.0 < max(het_gen) < 23.1):
        raise SystemExit("ABORTA: valores hetcap divergem")
    em_rows = csv_canonico("analytical-outputs/a04_contrastes_pareados.csv")
    em_dir = sum(1 for r in em_rows
                 if float(r["dif_pct_sintetica_10"]) != 0.0
                 and (float(r["dif_pct_real"]) > 0) == (float(r["dif_pct_sintetica_10"]) > 0))
    em_nulos = sum(1 for r in em_rows if float(r["dif_pct_sintetica_10"]) == 0.0)
    em_troca = sum(1 for r in csv_canonico("analytical-outputs/a04_tau_ranking.csv")
                   if r["primeiro_colocado_muda_vs_10"] == "True")
    em_pares = len(csv_canonico("analytical-outputs/a04_tau_ranking.csv"))
    if not (em_dir == 10 and em_nulos == 2 and em_troca == 1 and em_pares == 4):
        raise SystemExit(f"ABORTA: contagens EM {em_dir}/{em_nulos}/{em_troca}/{em_pares}")

    fig = plt.figure(figsize=(6.9, 3.45))
    gs = fig.add_gridspec(2, 6, hspace=0.62, wspace=0.9)
    axA = fig.add_subplot(gs[0, 0:2]); axB = fig.add_subplot(gs[0, 2:4])
    axC = fig.add_subplot(gs[0, 4:6]); axD = fig.add_subplot(gs[1, 0:3])
    axE = fig.add_subplot(gs[1, 3:6])
    axA.bar([0], [pue_ratio], width=0.45, color="#c05621")
    axA.axhline(1.0, color="#666666", lw=0.8, ls="--")
    axA.text(0, pue_ratio + 0.08, f"{pue_ratio:.3f}".replace(".", ",") + "×",
             ha="center", fontsize=9, fontweight="bold")
    axA.set_xticks([]); axA.set_ylim(0, 3.9)
    axA.set_title("(a) PUE invertido\n(contrafactual)", fontsize=8)
    axA.set_ylabel("razão de emissões (menor energia / menor emissão)", fontsize=7)
    axB.axhspan(-1, 1, color="#cbd5e0", alpha=0.5)
    ordem = ["cnemesis", "follow_renewables", "followme_s"]
    axB.scatter(range(3), [rede[a] for a in ordem], color="#2b6cb0", zorder=3, s=28)
    axB.axhline(0, color="#666666", lw=0.6)
    axB.set_xticks(range(3))
    axB.set_xticklabels(["c-NEMESIS", "FollowRenew.", "FollowMe-S"], fontsize=7)
    axB.set_ylim(-1.4, 1.4)
    axB.set_title("(b) rede restrita\n(emissão total; admissão desigual)", fontsize=8)
    axB.set_ylabel("Δ de emissão (%) vs. lowest_carbon_dc", fontsize=7)
    axB.text(1, -1.22, "faixa ±1%", fontsize=7, ha="center", color="#4a5568")
    for i, (nome, (ref, geo)) in enumerate(geo3.items()):
        y = 1 - i
        axC.plot([geo, ref], [y, y], color="#a0aec0", lw=2, zorder=2)
        axC.scatter([ref], [y], color="#2b6cb0", s=34, zorder=3)
        axC.scatter([geo], [y], color="#c05621", s=34, marker="D", zorder=3)
        axC.text(ref, y + 0.14, f"{ref:.2f}".replace(".", ","), fontsize=7, ha="center")
        axC.text(geo, y + 0.14, f"{geo:.2f}".replace(".", ","), fontsize=7, ha="center")
    axC.set_yticks([1, 0]); axC.set_yticklabels(["Azure", "Google"], fontsize=7)
    axC.set_ylim(-0.5, 1.6); axC.set_xlim(-4, 74)
    axC.set_title("(c) geo3 (descritivo;\ntopologia+capacidade)", fontsize=8)
    axC.set_xlabel("contraste dos mínimos de emissão (%): referência → geo3", fontsize=7)
    axD.barh([1], [max(het_gen) - min(het_gen)], left=[min(het_gen)], height=0.34,
             color="#cbd5e0")
    axD.scatter([het_lcdc], [0], color="#c05621", s=40, marker="D")
    axD.text(het_lcdc, 0.22, ("+" + f"{het_lcdc:.2f}" + "%").replace(".", ","), fontsize=7, ha="center")
    axD.text((min(het_gen) + max(het_gen)) / 2, 1.32,
             ("+" + f"{min(het_gen):.2f}" + "% a +" + f"{max(het_gen):.2f}" + "%").replace(".", ","),
             fontsize=7, ha="center")
    axD.set_yticks([1, 0]); axD.set_yticklabels(["políticas genéricas", "lowest_carbon_dc"], fontsize=7)
    axD.set_ylim(-0.6, 1.8); axD.set_xlim(0, 115)
    axD.set_title("(d) capacidade heterogênea (admissão integral)", fontsize=8)
    axD.set_xlabel("aumento de emissão vs. configuração correspondente do núcleo (%)", fontsize=7)
    axE.axis("off")
    axE.text(0.25, 0.62, f"{em_dir}/12", fontsize=17, fontweight="bold", ha="center")
    axE.text(0.25, 0.30, "direção preservada\n(2 empates no lado sintético)", fontsize=7, ha="center")
    axE.text(0.72, 0.62, f"{em_troca}/{em_pares}", fontsize=17, fontweight="bold", ha="center")
    axE.text(0.72, 0.30, "troca da política de\nmenor emissão total", fontsize=7, ha="center")
    axE.text(0.5, 0.02, "séries históricas (Electricity Maps); janela única de 4 h", fontsize=7,
             ha="center", color="#4a5568")
    axE.set_title("(e) séries históricas", fontsize=8)
    fig.tight_layout()
    f3 = out / "figures" / "fig-sensibilidade-cenarios.pdf"
    fig.savefig(f3, metadata={"CreationDate": None}, bbox_inches="tight", pad_inches=0.03)
    plt.close(fig)
    registro.append({"saida": "figures/fig-sensibilidade-cenarios.pdf",
                     "fontes": ["analytical-outputs/a10_razao_erro_proxy.csv",
                                "analytical-outputs/tost_equivalencia_1pct.csv",
                                "analytical-outputs/melhor_ca_vs_melhor_gb.csv",
                                "analytical-outputs/a06_geo3_recorte.csv",
                                "analytical-outputs/a17_delta_gco2_por_algoritmo.csv",
                                "analytical-outputs/a04_contrastes_pareados.csv",
                                "analytical-outputs/a04_tau_ranking.csv"],
                     "transformacao": "seleção de valores congelados com validação por painel; métricas naturais, sem escala comum; qualificadores nas anotações"})

    # ---- S1: macros numéricas -----------------------------------------------
    # Execuções e algoritmos extraídos da linha 'total:' do digest congelado:
    # seleção, não cálculo. Aborta se a linha mudar de forma.
    import re
    m = re.search(r"total:\s*(\d+) execuções, (\d+) células, (\d+) algoritmos",
                  le_canonico("analytical-outputs/digest.txt").read_text())
    if not m:
        raise SystemExit("ABORTA: linha 'total:' ausente do digest congelado")
    num_exec, num_cel_digest, num_algos = m.groups()
    # n=30 nas células cobertas pela run-013 (16, do digest); redundantes da
    # coluna redundante_por_construcao de a05_tau_10_vs_30.csv; 26 = 30 - 4
    m30 = re.search(r"n=30 sementes \((\d+) células\)",
                    le_canonico("analytical-outputs/digest.txt").read_text())
    if not m30:
        raise SystemExit("ABORTA: linha 'n=30 sementes (N células)' ausente do digest")
    n30 = int(m30.group(1))
    n10 = int(num_cel_digest) - n30
    redundantes = {r["celula"] for r in csv_canonico("analytical-outputs/a05_tau_10_vs_30.csv")
                   if r["redundante_por_construcao"] == "True"}
    n_red = len(redundantes)
    n_nao_red = int(num_cel_digest) - n_red
    if (n30, n10, n_red, n_nao_red) != (16, 14, 4, 26):
        raise SystemExit(f"ABORTA: contagens estruturais {n30}/{n10}/{n_red}/{n_nao_red} "
                         "diferem do declarado nos artefatos promovidos (16/14/4/26)")
    claims = csv_canonico("claims.csv")
    dist = {}
    for r in claims:
        dist[r["estado_final"]] = dist.get(r["estado_final"], 0) + 1
    macros = [
        "% GERADO por generate_paper_artifacts.py - NÃO EDITAR",
        "% Toda contagem vem de canonical-results/ (contagens derivadas, nunca à mão)",
        f"\\newcommand{{\\NumCelulas}}{{{len(classe_a) + len(classe_b) + len(integrais)}}}",
        f"\\newcommand{{\\NumCelulasClasseA}}{{{len(classe_a)}}}",
        f"\\newcommand{{\\NumCelulasClasseB}}{{{len(classe_b)}}}",
        f"\\newcommand{{\\NumCelulasIntegrais}}{{{len(integrais)}}}",
        f"\\newcommand{{\\NumCelulasComparaveis}}{{{len(classe_b) + len(integrais)}}}",
        f"\\newcommand{{\\NumClaims}}{{{len(claims)}}}",
        f"\\newcommand{{\\NumClaimsPreservadas}}{{{dist.get('preservada', 0)}}}",
        f"\\newcommand{{\\NumClaimsMagnitudeRevisada}}{{{dist.get('preservada_com_magnitude_revisada', 0)}}}",
        f"\\newcommand{{\\NumClaimsReformuladas}}{{{dist.get('reformulada', 0)}}}",
        f"\\newcommand{{\\NumClaimsRefutadas}}{{{dist.get('refutada_pelo_binario_canonico', 0)}}}",
        "% da linha 'total:' de analytical-outputs/digest.txt",
        f"\\newcommand{{\\NumExecucoes}}{{{int(num_exec):_d}}}".replace("_", "."),
        # mesma contagem, separador de milhar em inglês, para o abstract
        f"\\newcommand{{\\NumExecucoesEN}}{{{int(num_exec):,d}}}",
        f"\\newcommand{{\\NumAlgoritmos}}{{{num_algos}}}",
        # contagens estruturais derivadas mecanicamente
        f"\\newcommand{{\\NumCelulasNTrinta}}{{{n30}}}",
        f"\\newcommand{{\\NumCelulasNDez}}{{{n10}}}",
        f"\\newcommand{{\\NumCelulasRedundantes}}{{{n_red}}}",
        f"\\newcommand{{\\NumCelulasNaoRedundantes}}{{{n_nao_red}}}",
        "% unidades fixadas (provenance.json#politica_de_unidades):",
        "\\newcommand{\\UnidadeEmissao}{gCO$_2$e}",
        "\\newcommand{\\UnidadeMigBytes}{MiB}",
        "\\newcommand{\\MigracoesQualificador}{emitidas}",
        "",
    ]
    escreve(out / "text-snippets" / "macros.tex", "\n".join(macros), registro,
            ["claims.csv", "provenance.json#celulas", "provenance.json#politica_de_unidades"],
            "contagens por comprimento de lista e por frequência de campo; zero números digitados")

    # ---- provenance.json + manifesto ----------------------------------------
    prov = {
        "o_que_e": "Representações do artigo geradas a partir de canonical-results/, sem decisão científica nova.",
        "fonte_unica": "../canonical-results (verify rc=0 exigido antes de gerar)",
        "gerador": "generate_paper_artifacts.py",
        "politica_de_arredondamento": {"percentuais": ARREDONDA_PCT, "postos": ARREDONDA_POSTO},
        "guardas": ["leitura restrita a canonical-results/", "classe A nunca em contexto de vencedor",
                    "unidades proibidas: " + ", ".join(UNIDADES_PROIBIDAS)],
        "saidas": sorted(registro, key=lambda r: r["saida"]),
        "nao_gerado": {
            "figura-classe-A-tres-normalizadores": "os postos sob demanda admitida (N2) não estão em canonical-results/"
        },
    }
    (out / "provenance.json").write_text(
        json.dumps(prov, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    def sha256(p: Path) -> str:
        h = hashlib.sha256()
        with p.open("rb") as fh:
            for b in iter(lambda: fh.read(1 << 20), b""):
                h.update(b)
        return h.hexdigest()

    linhas = [f"{sha256(p)}  {p.relative_to(out)}"
              for p in sorted(out.rglob("*"))
              if p.is_file() and p.name != "SHA256SUMS.txt" and "__pycache__" not in p.parts]
    (out / "SHA256SUMS.txt").write_text("\n".join(linhas) + "\n")
    print(f"gerado em {out}: {len(registro)} artefatos + provenance.json + manifesto ({len(linhas)} arquivos)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

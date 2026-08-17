#!/usr/bin/env python3
"""Primitivas estatísticas usadas por mais de um cálculo.

Só entra aqui função cujo resultado precisa ser idêntico entre a reanálise
principal e os cálculos derivados do nível 3. Duas implementações de TOST que
diferem num detalhe dão dois p-valores para a mesma pergunta, e nada no
artefato denunciaria a diferença.
"""
import os

import numpy as np
from scipy import stats


def tost_pct(x, y, margem=0.01):
    """TOST por Wilcoxon unilateral deslocado, com margem relativa à média de `y`.

    Retorna o maior dos dois p-valores unilaterais, já que a equivalência só é
    aceita se os dois testes rejeitarem.

    `margem` é relativa (0,01 = ±1%) e ancorada na média do braço de referência
    `y`. Ancorar na referência, e não na média das duas séries, evita que a
    margem se desloque junto com o efeito sob teste.

    Devolve NaN quando o teste não pode ser computado, tipicamente com série de
    diferenças identicamente nula, em que o Wilcoxon não tem posto não nulo.
    Quem chama decide o que fazer com o NaN.
    """
    d = np.asarray(x, dtype=float) - np.asarray(y, dtype=float)
    m = margem * np.mean(y)
    try:
        p_lo = stats.wilcoxon(d + m, alternative="greater", zero_method="wilcox").pvalue
        p_hi = stats.wilcoxon(d - m, alternative="less", zero_method="wilcox").pvalue
    except Exception:
        return float("nan")
    return max(p_lo, p_hi)


def wsr_p(d):
    """Wilcoxon signed-rank bilateral, exato quando possível.

    Diferenças identicamente nulas devolvem p = 1: não há evidência de
    diferença e a vaga na família é preservada."""
    d = np.asarray(d, dtype=float)
    if np.all(d == 0):
        return 1.0
    try:
        return stats.wilcoxon(d, zero_method="wilcox", method="exact").pvalue
    except Exception:
        return stats.wilcoxon(d, zero_method="wilcox", method="auto").pvalue


def rank_biserial(d):
    d = np.asarray(d, dtype=float)
    d = d[d != 0]
    if len(d) == 0:
        return 0.0
    postos = stats.rankdata(np.abs(d))          # midrank
    return (postos[d > 0].sum() - postos[d < 0].sum()) / postos.sum()


def piso_wsr(n):
    """Menor p bilateral exato do Wilcoxon signed-rank com n pares não nulos.

    Só as duas configurações extremas de sinais atingem a estatística mínima,
    então o menor p bilateral é 2/2**n. Depende apenas de n, e portanto é
    conhecido antes de olhar os dados.
    """
    return 2.0 / (2 ** n)


# O único par com degenerescência estrutural documentada é
# (follow_renewables, lowest_carbon_dc), e apenas nas células em que o critério
# suficiente a priori é satisfeito.
PAR_ESTRUTURAL = ("follow_renewables", "lowest_carbon_dc")


def celulas_degeneradas(exp_dir):
    """Células com degenerescência estrutural, lidas do critério a priori.

    Nunca inferidas dos resultados: igualdade observada sem critério estrutural
    é empate empírico, e empate empírico não reduz família (RE-2). Sem o arquivo
    de critério devolve conjunto vazio, que é o lado conservador; quem chama
    decide se isso serve.
    """
    caminho = os.path.join(exp_dir, "analysis", "inputs",
                           "criterio-a-priori.txt")
    if not os.path.isfile(caminho):
        return set(), caminho
    estruturais = set()
    for linha in open(caminho):
        if "DEGENERADO POR CONSTRUCAO NO CENARIO" in linha:
            estruturais.add(linha.split()[0])
    return estruturais, caminho


def postos_medios(valores):
    """{rótulo: valor} -> {rótulo: posto médio}, menor valor = melhor posto.

    Midrank puro: empatados dividem a média das posições que ocupariam. Sem
    desempate por identificador, ordem de linha ou ordem de entrada, conforme
    RE-2. A igualdade é testada por igualdade exata de ponto flutuante.
    """
    rotulos = list(valores)
    vals = np.array([valores[r] for r in rotulos], dtype=float)
    postos = stats.rankdata(vals, method="average")
    return dict(zip(rotulos, postos))


def vencedores(postos):
    """Conjunto de rótulos no posto mínimo. Com empate devolve todos; a ordem
    alfabética é só apresentação, não desempate."""
    minimo = min(postos.values())
    return sorted(r for r, p in postos.items() if p == minimo)


def holm(pv):
    """Holm-Bonferroni sobre a família completa recebida.

    O tamanho da família vem do desenho, nunca dos dados: contrastes degenerados
    entram com p = 1 e ocupam sua vaga (RE-2).
    """
    p = np.asarray(pv, dtype=float)
    m = len(p)
    ordem = np.argsort(p)
    adj = np.empty(m)
    corrente = 0.0
    for k, idx in enumerate(ordem):
        corrente = max(corrente, (m - k) * p[idx])
        adj[idx] = min(1.0, corrente)
    return adj

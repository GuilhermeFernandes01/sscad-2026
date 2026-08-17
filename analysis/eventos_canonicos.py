#!/usr/bin/env python3
"""Leitura dos eventos brutos das campanhas canônicas.

`decisions.jsonl` é o registro por evento de cada execução, uma linha JSON por
decisão: {"t", "kind", "vm_id", "src", "dst", "reason"}. O nome do hospedeiro é
"<dc>-hNNN", de modo que o data center é o prefixo antes de "-h".

Duas afirmações do nível 3 descem ao nível de evento, A-17 (fração de alocações
no data center limpo sob capacidade heterogênea) e A-18 (perfil intra/inter data
center). A localização dos arquivos fica aqui para que nenhuma das duas a
reimplemente: um erro de caminho leria execuções fora das runs canônicas sem que
o número resultante denunciasse a troca.

Regras:

- Só as quatro runs canônicas são varridas; nenhum outro diretório em `runs/`
  pode ser lido por cálculo que embase conclusão.
- A busca é exaustiva e levanta em vez de escolher: se a mesma tripla (célula,
  algoritmo, semente) existir em mais de uma run canônica, é erro. As runs
  canônicas cobrem conjuntos de sementes disjuntos por construção, e uma
  duplicata significaria que essa premissa quebrou.
"""
import json
import os

RUNS_CANONICAS = [
    "run-010-fase1-canonica-seeds42-51",
    "run-011-fase23-canonica-seeds42-51",
    "run-012-em2021-canonica-seeds42-51",
    "run-013-fase1-canonica-seeds52-71",
]

HERE = os.path.dirname(os.path.abspath(__file__))
EXP = os.path.normpath(os.path.join(HERE, ".."))
RUNS = os.path.join(EXP, "runs")


def dc_de(host):
    """'paris-h003' -> 'paris'. Vale para src e dst dos eventos."""
    return host.rsplit("-h", 1)[0]


def caminho_eventos(celula, algoritmo, seed):
    """Localiza o decisions.jsonl da execução nas runs canônicas.

    Devolve (caminho, run_id). Levanta se não houver exatamente um.
    """
    achados = []
    for run_id in RUNS_CANONICAS:
        p = os.path.join(RUNS, run_id, "raw", celula, algoritmo,
                         f"seed_{seed}", "decisions.jsonl")
        if os.path.isfile(p):
            achados.append((p, run_id))
    if not achados:
        raise SystemExit(f"eventos ausentes nas runs canônicas: "
                         f"{celula}/{algoritmo}/seed_{seed}")
    if len(achados) > 1:
        raise SystemExit(f"eventos duplicados em runs canônicas para "
                         f"{celula}/{algoritmo}/seed_{seed}: "
                         f"{[r for _, r in achados]}")
    return achados[0]


def eventos(celula, algoritmo, seed):
    """Itera os eventos da execução. Levanta em linha malformada."""
    caminho, _ = caminho_eventos(celula, algoritmo, seed)
    with open(caminho) as fh:
        for i, linha in enumerate(fh, 1):
            try:
                yield json.loads(linha)
            except json.JSONDecodeError as e:
                raise SystemExit(f"{caminho}:{i}: linha malformada ({e})")

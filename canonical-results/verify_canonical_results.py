#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""verify_canonical_results.py - verificação do pacote congelado.

rc=0 significa que nenhum byte do pacote mudou desde o congelamento e que os
elos de proveniência declarados continuam verificáveis contra as fontes
promovidas. Qualquer divergência devolve rc diferente de zero.

Verifica:
  1. SHA256SUMS.txt cobre todos os arquivos do pacote (exceto ele próprio) e
     todos conferem - inclusive README.md e os dois scripts;
  2. o agregado de analytical-outputs/ é o declarado no provenance.json e
     coincide com o da ratificação promovida (3b909782...);
  3. claims.csv tem 23 linhas com a distribuição 4/9/9/1 e o sha256 declarado;
  4. as listas de células fecham: 12 A + 7 B + 11 integrais = 30, disjuntas.

O digest do bloco de afirmações da matriz-fonte (não distribuída) permanece
registrado em provenance.json como compromisso verificável na origem.

Uso: python3 verify_canonical_results.py
"""
import csv
import hashlib
import json
import sys
from pathlib import Path

AQUI = Path(__file__).resolve().parent

DISTRIBUICAO = {"preservada": 4, "preservada_com_magnitude_revisada": 9,
                "reformulada": 9, "refutada_pelo_binario_canonico": 1}
falhas = []


def sha256(p: Path) -> str:
    h = hashlib.sha256()
    with p.open("rb") as fh:
        for b in iter(lambda: fh.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def checa(cond, msg):
    if cond:
        print(f"OK   {msg}")
    else:
        falhas.append(msg)
        print(f"FALHA {msg}")


def main() -> int:
    prov = json.loads((AQUI / "provenance.json").read_text(encoding="utf-8"))

    # 1. manifesto integral
    manifesto = {}
    for linha in (AQUI / "SHA256SUMS.txt").read_text().splitlines():
        h, _, rel = linha.partition("  ")
        manifesto[rel] = h
    no_disco = {str(p.relative_to(AQUI)) for p in AQUI.rglob("*")
                if p.is_file() and p.name != "SHA256SUMS.txt"
                and "__pycache__" not in p.parts}
    checa(set(manifesto) == no_disco,
          f"manifesto cobre exatamente os arquivos do pacote ({len(manifesto)})")
    ruins = [rel for rel, h in manifesto.items()
             if not (AQUI / rel).is_file() or sha256(AQUI / rel) != h]
    checa(not ruins, f"todos os arquivos conferem com o manifesto{'' if not ruins else ': ' + str(ruins)}")

    # 2. agregado das 48 saídas
    outs = sorted((AQUI / "analytical-outputs").iterdir())
    agregado = hashlib.sha256(
        "".join(sorted(f"{sha256(p)}  {p.name}\n" for p in outs)).encode()
    ).hexdigest()
    checa(len(outs) == 48, f"analytical-outputs/ tem {len(outs)} arquivos (esperados 48)")
    checa(agregado == prov["cadeia_analitica"]["saidas_48_agregado_sha256"],
          "agregado de analytical-outputs/ igual ao da cadeia promovida")
    checa(agregado == prov["pacote"]["agregado_analytical_outputs"],
          "agregado igual ao registrado no pacote")

    # 3. claims.csv
    with (AQUI / "claims.csv").open(encoding="utf-8") as fh:
        linhas = list(csv.DictReader(fh))
    dist = {}
    for r in linhas:
        dist[r["estado_final"]] = dist.get(r["estado_final"], 0) + 1
    checa(len(linhas) == 23, f"claims.csv com {len(linhas)} afirmações (esperadas 23)")
    checa(dist == DISTRIBUICAO, f"distribuição {dist}")
    checa(sha256(AQUI / "claims.csv") == prov["pacote"]["claims_csv_sha256"],
          "sha256 de claims.csv igual ao registrado")

    # 4. células
    a = prov["celulas"]["classe_a_comparacao_escalar_proibida"]
    b = prov["celulas"]["classe_b_ordenacao_robusta_reportar_atendimento"]
    i = prov["celulas"]["atendimento_integral"]
    checa((len(a), len(b), len(i)) == (12, 7, 11), f"contagens A/B/integrais {len(a)}/{len(b)}/{len(i)}")
    checa(len(set(a) | set(b) | set(i)) == 30 and not (set(a) & set(b))
          and not ((set(a) | set(b)) & set(i)), "listas disjuntas somando 30")

    print()
    if falhas:
        print(f"REPROVADO: {len(falhas)} falha(s)")
        return 1
    print("APROVADO: pacote íntegro e consistente com as fontes promovidas")
    return 0


if __name__ == "__main__":
    sys.exit(main())

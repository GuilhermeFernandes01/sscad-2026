#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""As cinco verificações de paper-generated/.

  1. proveniência: toda saída consta de provenance.json e toda fonte declarada
     está dentro de canonical-results/;
  2. determinismo: regenera tudo em diretório temporário e exige hashes
     idênticos, arquivo a arquivo;
  3. cobertura numérica: cada macro numérica de text-snippets/macros.tex é
     recontada aqui a partir do pacote canônico, sem passar pelo gerador;
  4. unidade: as strings vetadas (kgCO*, GiB, 'migrações concluídas') não
     aparecem, e as unidades obrigatórias aparecem;
  5. legalidade: nenhuma célula da classe A aparece em artefato de vencedor ou
     ranking.

rc=0 somente se as cinco passarem. Uso: python3 verify_paper_generated.py
"""
import csv
import hashlib
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

AQUI = Path(__file__).resolve().parent
CANON = AQUI.parent / "canonical-results"
falhas = []


def checa(cond, msg):
    print(("OK   " if cond else "FALHA ") + msg)
    if not cond:
        falhas.append(msg)


def sha256(p: Path) -> str:
    h = hashlib.sha256()
    with p.open("rb") as fh:
        for b in iter(lambda: fh.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def main() -> int:
    prov = json.loads((AQUI / "provenance.json").read_text(encoding="utf-8"))

    # 0. pré-condição: o pacote canônico continua íntegro
    rc = subprocess.run([sys.executable, str(CANON / "verify_canonical_results.py")],
                        capture_output=True).returncode
    checa(rc == 0, "pré-condição: verify_canonical_results rc=0")

    # 1. proveniência
    declaradas = {r["saida"] for r in prov["saidas"]}
    no_disco = {str(p.relative_to(AQUI)) for p in AQUI.rglob("*") if p.is_file()
                and p.parent.name in ("tables", "figures", "text-snippets")}
    checa(declaradas == no_disco,
          f"toda saída declarada em provenance.json ({sorted(declaradas ^ no_disco) or 'exato'})")
    fontes_ok = all((CANON / f.split("#")[0]).is_file()
                    for r in prov["saidas"] for f in r["fontes"])
    checa(fontes_ok, "toda fonte declarada existe dentro de canonical-results/")

    # 2. determinismo: regeneração completa em diretório temporário
    with tempfile.TemporaryDirectory() as td:
        r = subprocess.run([sys.executable, str(AQUI / "generate_paper_artifacts.py"),
                            "--outdir", td], capture_output=True, text=True)
        checa(r.returncode == 0, "regeneração terminou com rc=0")
        dif = []
        for rel in sorted(declaradas):
            a, b = AQUI / rel, Path(td) / rel
            if not b.is_file() or sha256(a) != sha256(b):
                dif.append(rel)
        checa(not dif, f"regeneração byte a byte idêntica{'' if not dif else ': difere ' + str(dif)}")

    # 3. cobertura numérica: recontagem independente
    canon_prov = json.loads((CANON / "provenance.json").read_text())
    ca = canon_prov["celulas"]["classe_a_comparacao_escalar_proibida"]
    cb = canon_prov["celulas"]["classe_b_ordenacao_robusta_reportar_atendimento"]
    ci = canon_prov["celulas"]["atendimento_integral"]
    with (CANON / "claims.csv").open(encoding="utf-8") as fh:
        claims = list(csv.DictReader(fh))
    dist = {}
    for c in claims:
        dist[c["estado_final"]] = dist.get(c["estado_final"], 0) + 1
    esperado = {
        "NumCelulas": len(ca) + len(cb) + len(ci), "NumCelulasClasseA": len(ca),
        "NumCelulasClasseB": len(cb), "NumCelulasIntegrais": len(ci),
        "NumCelulasComparaveis": len(cb) + len(ci), "NumClaims": len(claims),
        "NumClaimsPreservadas": dist.get("preservada", 0),
        "NumClaimsMagnitudeRevisada": dist.get("preservada_com_magnitude_revisada", 0),
        "NumClaimsReformuladas": dist.get("reformulada", 0),
        "NumClaimsRefutadas": dist.get("refutada_pelo_binario_canonico", 0),
    }
    # recontagem independente da linha 'total:' do digest congelado
    mdig = re.match(r"(\d+) execuções, (\d+) células, (\d+) algoritmos",
                    next(l.split("total: ")[1] for l in
                         (CANON / "analytical-outputs" / "digest.txt").read_text().splitlines()
                         if "total:" in l))
    esperado["NumExecucoes"] = int(mdig.group(1))
    esperado["NumAlgoritmos"] = int(mdig.group(3))
    # recontagem independente das contagens estruturais
    dig = (CANON / "analytical-outputs" / "digest.txt").read_text()
    m30 = re.search(r"n=30 sementes \((\d+) células\)", dig)
    esperado["NumCelulasNTrinta"] = int(m30.group(1))
    esperado["NumCelulasNDez"] = int(mdig.group(2)) - int(m30.group(1))
    with (CANON / "analytical-outputs" / "a05_tau_10_vs_30.csv").open() as fh:
        red = {r["celula"] for r in csv.DictReader(fh)
               if r["redundante_por_construcao"] == "True"}
    esperado["NumCelulasRedundantes"] = len(red)
    esperado["NumCelulasNaoRedundantes"] = int(mdig.group(2)) - len(red)
    macros = dict(re.findall(r"\\newcommand\{\\(\w+)\}\{([\d.]+)\}",
                             (AQUI / "text-snippets" / "macros.tex").read_text()))
    erros = [k for k, v in esperado.items()
             if int(macros.get(k, "-1").replace(".", "")) != v]
    checa(not erros, f"macros numéricas conferem com recontagem{'' if not erros else ': ' + str(erros)}")

    # 4. unidade
    textos = "".join(p.read_text(encoding="utf-8", errors="replace")
                     for p in AQUI.rglob("*.tex"))
    for probe in ("kgCO", "GiB", "migrações concluídas", "migracoes concluidas"):
        checa(probe not in textos, f"unidade proibida ausente: {probe}")
    checa("gCO$_2$e" in textos, "unidade de emissão declarada (gCO2e) presente")

    # 5. legalidade
    ofensas = []
    for p in list(AQUI.glob("tables/*.tex")) + list(AQUI.glob("text-snippets/*.tex")):
        t = p.read_text(encoding="utf-8")
        if "vencedor" not in t.lower() and "ranking" not in t.lower():
            continue
        for cel in ca:
            if re.search(rf"^{re.escape(cel.replace('_', chr(92) + '_'))}\s*&", t, re.M):
                ofensas.append(f"{p.name}: {cel}")
    checa(not ofensas, f"nenhuma célula classe A em contexto de vencedor{'' if not ofensas else ': ' + str(ofensas)}")

    print()
    if falhas:
        print(f"REPROVADO: {len(falhas)} falha(s)")
        return 1
    print("APROVADO: as cinco verificações passaram")
    return 0


if __name__ == "__main__":
    sys.exit(main())

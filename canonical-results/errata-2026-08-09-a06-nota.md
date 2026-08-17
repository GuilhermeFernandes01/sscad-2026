# Errata da nota de A-06 (contagem de data centers)

Na nota de A-06 em `claims.csv`, onde se lê "ao passar de 5 para 3 data centers", leia-se "ao passar de 9 para 3 data centers". É um lapso de redação: os valores (58,74% -> 2,90% e 65,99% -> 6,40%), o estado `refutada_pelo_binario_canonico` e a conclusão do claim continuam os mesmos.

A evidência está no `config.yaml` congelado das runs promovidas, que declara 9 `carbon_series_id` na célula de referência (`go-realjun-folgada`, run-010) e 3 na célula geo3 (`geo3-go-realjun-folgada`, run-011).

O `claims.csv` não foi editado. A correção entra por acréscimo, para preservar o arquivo original, que segue trazendo "5" naquela passagem; quem citar a nota precisa citar esta errata junto.

Registrada em 2026-08-09.

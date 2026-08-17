# Perfis sinteticos anti-correlacionados

Gerados por `carbon_profiles.py` (ver sidecars `.meta.yaml`). Parametros calibrados nas 9 series reais de 2020 de `data/carbon/{canberra,seoul,paris,virginia,dubai,singapore,pune,johannesburg,sp}.csv`:

- media pooled = 463.1 gCO2/kWh
- desvio-padrao pooled = 307.0 -> amplitude = 307.0 * sqrt(2) = 434.1 (o desvio-padrao de uma senoide e amplitude/sqrt(2))
- fase por DC = longitude / 15 h (hora solar local), garantindo cruzamentos temporais da ordem de carbono entre regioes ao longo de 24 h

Comando exato de geracao:

    python3 -m algosim_analysis.carbon_profiles anticorr \
      --mean 463.1 --amplitude 434.1 --hours 8760 \
      --dc canberra:9.94 --dc seoul:8.47 --dc singapore:6.92 --dc pune:4.92 \
      --dc dubai:3.68 --dc johannesburg:1.87 --dc paris:0.16 --dc sp:-3.11 \
      --dc virginia:-5.24 --out-dir data/carbon/anticorr

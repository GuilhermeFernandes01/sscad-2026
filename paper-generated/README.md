# Paper artifact generator

`generate_paper_artifacts.py` reads only `../canonical-results/` and produces the paper's tables (`tables/`), figures (`figures/`) and count macros (`text-snippets/macros.tex`). Every plotted value is validated against the frozen outputs; a mismatch aborts. `verify_paper_generated.py` re-runs the generation and requires byte-identical results (rc=0).

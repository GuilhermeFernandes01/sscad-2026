# Workload traces

Both files come from the public artifact of Vasconcelos et al. (SMARTGREENS 2022), directory simulations-algorithms/input/workload/, and are byte identical to the originals except for the final control line, where the stop timestamp was changed from 604500 to 604800.

- azure_2020.txt: derived by the original authors from the Azure Packing Trace 2020 (Hadary et al., OSDI 2020).
- google_2011.txt: derived by the original authors from the Google cluster trace (Reiss et al., 2011).

Line format: `vmdispatcher sendVM <t_seconds> <vm_id> <cores> <demand_mips>`. The conversion of the original trace fields to MIPS demand was performed by the original authors and is inherited here unchanged.

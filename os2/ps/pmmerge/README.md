<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# PMMERGE.DLL

PM merged library entries that only PMMERGE exports.

- IBM source: `E:\w45ps\src\ps\pmmerge`.
- Evidence: SYM directory `ps/pmmerge`; BLDLEVEL "OS/2 PM Merged Library".
- In Warp 4.52: The merged PMWIN + PMGRE + shell API + window controls library.
- In 2ine: Implements the PMGRE engine entries (`pmgre/`) and the
  PMMERGE-only entries; PMGRE forwards to it.
- Exports: `pmmerge.def`.

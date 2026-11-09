This directory contains Deark's FTCOMP/PACK2 decoder and the IBM LZW/LZMW
PACK decoder from its dskdcmps component. Deark is by Jason Summers; the
FTCOMP algorithm implementation credits Dimitriy Ryazantcev. The original
dskdcmps author released that component into the public domain.

Sources: https://github.com/jsummers/deark (`src/fmtutil-ftcomp.c`,
`foreign/ftcompdata.h`, `foreign/dskdcmps.h`). The retained source came from
the workspace's Deark checkout. `ftcomp.inc` drops Deark's own private-header includes:
the including unit, `storage/archives/ibm_pack/decode.c`, first includes the bounded, per-request
adapter in `storage/archives/ibm_pack/deark_shim.h`, then this decoder, rather than compiling it as
a standalone source.

`COPYING` retains Deark's MIT-style license. `readme-dskdcmps.txt` retains
the original component notice. The packaged application includes Deark's
license under its Licenses directory. No original OS/2 support-file payload
is included here.

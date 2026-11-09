<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# SO32DLL.DLL

16-bit-stack-compatible socket API set (inferred).

- IBM source: Unknown.
- Evidence: `debug/symbols/mptn/dll/so32dll.sym`.
- In Warp 4.52: Separate TCP/IP product; only the SYM files are on the client
  media. The SYM lists real code (socket16, bind16, _bindMSC), not a forwarder.
- In 2ine: `module.c` holds the whole DLL; it shares socket errors
  (`socket_errors.c`), socket layouts and gethostid (`network.c`) with
  TCPIP32 through `os2/mptn/common/`.
- Exports: `so32dll.def`.

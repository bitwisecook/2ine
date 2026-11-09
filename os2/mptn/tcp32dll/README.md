<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# TCP32DLL.DLL

16-bit-stack-compatible TCP utility API set (inferred).

- IBM source: Unknown.
- Evidence: `debug/symbols/mptn/dll/tcp32dll.sym`.
- In Warp 4.52: Separate TCP/IP product; only the SYM files are on the client
  media. The SYM lists real code (gethostbyname_r, Rgetsockname16), not a forwarder.
- In 2ine: `module.c` holds the whole DLL; it shares host lookups (`resolver.c`) with
  TCPIP32 through `os2/mptn/common/`.
- Exports: `tcp32dll.def`.

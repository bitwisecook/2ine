<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# DOSCALLS.DLL

Base OS API: files, memory, threads, processes, semaphores, modules, exceptions, time and the 16-bit Dos16 entries.

- IBM source: `E:\w45baseos\…\baseos\os2krnl\{task,memory,filesys,sem,loader,dev,perf}\` for the kernel API workers; DOSCALL1's own directory is unknown (inferred `w45baseos\…\baseos\doscall1`).
- Evidence: Kernel MAPs (`debug/{UNI,SMP}/os2krnl*.map`); DOSCALL1 MAP has segments only.
- In Warp 4.52: DOSCALLS is the kernel's built-in module; ordinals it does not implement resolve into DOSCALL1.DLL.
- Exports: `doscalls.def`.

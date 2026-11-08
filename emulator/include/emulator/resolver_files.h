/* Public domain. No rights reserved. Wholly AI-authored. */
/* The managed resolver files (HOSTS, RESOLV and RESOLV2) under the guest ETC directory. */
#ifndef TWOINE_EMULATOR_RESOLVER_FILES_H
#define TWOINE_EMULATOR_RESOLVER_FILES_H
/* Rewrite the files this runtime generated from the host network configuration;
   files without the generated marker are left as the user configured them. */
void lxPrepareNetworkConfiguration(void);
#endif

# Public domain. No rights reserved. Wholly AI-authored.
# OpenSSL TLS transport. host/CMakeLists.txt links the OpenSSL build that
# matches each worker's ABI.
twoine_host_function(net net/tls.c)

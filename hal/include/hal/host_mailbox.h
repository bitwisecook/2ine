/* Public domain. No rights reserved. Wholly AI-authored. */
/* Named local mailboxes exchange bounded opaque records between host processes. */
#ifndef TWOINE_HOST_MAILBOX_H
#define TWOINE_HOST_MAILBOX_H
#include <stddef.h>
#include <stdint.h>
typedef struct LxHostMailbox LxHostMailbox;
/* Result: 0 success, 1 missing/empty, 2 already exists, -1 failure. */
int lx_host_mailbox_open(const char *directory, const char *name, int create, unsigned order,
                         LxHostMailbox **output, uint32_t *owner);
void lx_host_mailbox_close(LxHostMailbox *mailbox);
int lx_host_mailbox_send(LxHostMailbox *mailbox, const void *data, size_t size, unsigned priority);
/* Receive returns 2 for an unknown token, 3 at end of a peek cursor.
   peek=2 selects the successor of token; peek=1 selects that token. */
int lx_host_mailbox_receive(LxHostMailbox *mailbox, void *data, size_t capacity, size_t *size,
                            unsigned *priority, uint32_t *token, int peek);
int lx_host_mailbox_count(LxHostMailbox *mailbox, uint32_t *count, int purge);
/* Revision changes on every successful send, including when readers consume data. */
int lx_host_mailbox_revision(LxHostMailbox *mailbox, uint32_t *revision);
#endif

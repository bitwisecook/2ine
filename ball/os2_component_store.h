/* Public domain. No rights reserved. Wholly AI-authored. */
/* Component jobs persist progress atomically and journal each committed file. */
#ifndef TB_BALL_OS2_COMPONENT_STORE_H
#define TB_BALL_OS2_COMPONENT_STORE_H
#include "ball/store.h"

struct ComponentOperation
{
    TbMutationLock *lock;
    const char *id, *progress_path, *cancel_path, *log_path, *journal_path;
    cJSON *progress, *ledger;
    int cancelled;
};

int component_token(const char *token);
char *component_key(TbBall *c, const char *guest);
cJSON *component_read(TbBall *c, const char *path);
int component_storage(TbBall *c, const char *relative);
cJSON *component_ledger(TbBall *c);
int component_same_identity(TbBall *c, const cJSON *record, const TbFileIdentity *identity);
void component_progress(TbBall *c, const char *phase, const char *source, const char *path);
int component_cancelled(TbBall *c);
int component_journal(TbBall *c, cJSON *entry);
int component_record(TbBall *c, const char *guest, const char *source, const char *category,
                     const char *hash);
int component_begin(TbBall *c, const cJSON *request, const char *kind);
void component_finish(TbBall *c, cJSON *result);
char *component_private_path(TbBall *c, const char *guest);
#endif

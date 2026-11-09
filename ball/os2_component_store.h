/* Public domain. No rights reserved. Wholly AI-authored. */
/* Component jobs persist progress atomically and journal each committed file. */
#ifndef TB_BALL_OS2_COMPONENT_STORE_H
#define TB_BALL_OS2_COMPONENT_STORE_H
#include "ball/ball_internal.h"

struct ComponentOperation
{
    TbMutationLock *lock;
    const char *id, *progress_path, *cancel_path, *log_path, *journal_path;
    cJSON *progress, *ledger;
    int cancelled;
};

int component_token(const char *token);
char *component_key(Context *c, const char *guest);
cJSON *component_read(Context *c, const char *path);
int component_storage(Context *c, const char *relative);
cJSON *component_ledger(Context *c);
int component_same_identity(Context *c, const cJSON *record, const TbFileIdentity *identity);
void component_progress(Context *c, const char *phase, const char *source, const char *path);
int component_cancelled(Context *c);
int component_journal(Context *c, cJSON *entry);
int component_record(Context *c, const char *guest, const char *source, const char *category,
                     const char *hash);
int component_begin(Context *c, const cJSON *request, const char *kind);
void component_finish(Context *c, cJSON *result);
char *component_private_path(Context *c, const char *guest);
#endif

/* Public domain. No rights reserved. Wholly AI-authored. */
/* The 2ine.cfg projection of the drive table and system settings. */
#include "ball/store.h"
#include "emulator/launch/runtime_config.h"
#include "emulator/launch/settings_schema.h"

int launch_write_runtime_config(TbBall *c)
{
    if (!tb_ball_roots(c) || !launch_validate_system(c))
        return 0;
    char *text = tb_ball_keep(c, strdup(""));
    for (int i = 0; i < TB_BALL_DRIVE_COUNT && text; i++)
        text = tb_ball_format(c, "%smountpoint.%c = %s\n", text, 'A' + i,
                              c->roots[i] ? c->roots[i] : "");
    cJSON *entry;
    cJSON_ArrayForEach(entry, get(c->manifest, "system"))
    {
        text = tb_ball_format(c, "%ssystem.%s = %s\n", text, entry->string,
                              launch_setting_text(c, entry));
        if (!text)
            return 0;
    }
    return text && tb_atomic_write(tb_ball_join(c, c->ball, "2ine.cfg"), text, strlen(text))
               ? 1
               : tb_ball_fail(c, "Cannot write 2ine.cfg");
}

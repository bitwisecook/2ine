/* Public domain. No rights reserved. Wholly AI-authored. */
/* Normalisation of member paths inside image volumes. */
#ifndef TB_STORAGE_MEMBER_PATH_H
#define TB_STORAGE_MEMBER_PATH_H

/* Components a member path may keep after normalisation. */
#define TB_MEMBER_PATH_DEPTH 256

/* Normalises a member path, in which '/' separates components (the runtime has already
   translated OS/2 '\' separators). Empty and "." components are dropped and ".." removes the
   previous component; case is kept. Returns an owned string of the remaining components
   joined by '/', with no leading or trailing '/' ("" for the root), or NULL with errno:
   EINVAL for a NULL member or one containing '\' or ':', which OS/2 names cannot hold;
   EACCES when ".." would leave the root; ENAMETOOLONG past TB_MEMBER_PATH_DEPTH
   components. */
char *tb_member_path(const char *member);
#endif

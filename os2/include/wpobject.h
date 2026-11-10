/* Public domain. No rights reserved. Wholly AI-authored. */
/* WPObject method values the Workplace Shell classes share: class styles,
   views, delete and name-clash results, object references and menu IDs. */
#ifndef _INCL_WPOBJECT_H_
#define _INCL_WPOBJECT_H_

#ifdef __cplusplus
extern "C"
{
#endif

    /* Class styles (wpclsQueryStyle) */
    enum
    {
        CLSSTYLE_NEVERMOVE = 0x00000002,
        CLSSTYLE_NEVERCOPY = 0x00000008,
        CLSSTYLE_NEVERTEMPLATE = 0x00000010,
        CLSSTYLE_NEVERDELETE = 0x00000040,
        CLSSTYLE_NEVERPRINT = 0x00000080,
        CLSSTYLE_PRIVATE = 0x00001000
    };

    /* wpSetMinWindow values */
    enum
    {
        MINWIN_VIEWER = 2
    };

    /* wpSetButtonAppearance values */
    enum
    {
        DEFAULTBUTTON = 3
    };

    /* wpObjectReady flags */
    enum
    {
        OR_REFERENCE = 0x10000000,
        OR_FROMTEMPLATE = 0x00000004 | OR_REFERENCE,
        OR_FROMCOPY = 0x00000008 | OR_REFERENCE,
        OR_SHADOW = 0x00000010 | OR_REFERENCE
    };

    /* Menu item IDs */
    enum
    {
        WPMENUID_PRIMARY = 0,
        WPMENUID_DELETE = 109,
        WPMENUID_PROPERTIES = 112,
        WPMENUID_CLOSE = 120
    };

    /* wpDelete results */
    enum
    {
        OK_DELETE = 1,
        NO_DELETE = 2,
        CANCEL_DELETE = 3
    };

    /* Confirm flags */
    enum
    {
        CONFIRM_DELETE = 0x0001,
        CONFIRM_DELETEFOLDER = 0x0002
    };

    /* wpOpen views */
    enum
    {
        OPEN_CONTENTS = 1,
        OPEN_HELP = 3,
        OPEN_RUNNING = 4
    };

    /* wpConfirmObjectTitle return values */
    enum
    {
        NAMECLASH_CANCEL = 0x00,
        NAMECLASH_NONE = 0x01,
        NO_NAMECLASH_APPEND = 0x20
    };

    /* wpDrop method return codes */
    enum
    {
        RC_DROP_ITEMCOMPLETE = 1
    };

    /* wpFindViewItem view flags */
    enum
    {
        VIEW_CONTENTS = 0x00000001,
        VIEW_SETTINGS = 0x00000002,
        VIEW_HELP = 0x00000004,
        VIEW_RUNNING = 0x00000008
    };

#ifdef __cplusplus
}
#endif

#endif

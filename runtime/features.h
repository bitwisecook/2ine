/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_RUNTIME_FEATURES_H
#define TWOINE_RUNTIME_FEATURES_H
typedef enum LxRuntimeFeature
{
    LX_FEATURE_VIDEO,
    LX_FEATURE_PCM,
    LX_FEATURE_MIDI,
    LX_FEATURE_DIVE,
    LX_FEATURE_COUNT
} LxRuntimeFeature;
enum
{
    LX_OPENGL_OFF,
    LX_OPENGL_MESA,
    LX_OPENGL_ORIGINAL
};
enum
{
    LX_CHROME_NATIVE,
    LX_CHROME_OS2,
    LX_CHROME_BOTH
};
int lxRuntimeFeatureEnabled(LxRuntimeFeature feature);
int lxRuntimeOpenGLProvider(void);
int lxRuntimeWindowChrome(void);
/* Configuration is read before guest threads start. */
int lxRuntimeConfigureFeature(const char *name, const char *value);
#endif

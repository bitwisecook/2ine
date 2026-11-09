/* Public domain. No rights reserved. Wholly AI-authored. */
/* Record the linked FFmpeg library's actual provider, configuration and license. */
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <stdio.h>
#include <string.h>

static void string(FILE *file, const char *text)
{
    fputc('"', file);
    for (const unsigned char *p = (const unsigned char *)text; *p; p++)
    {
        if (*p == '"' || *p == '\\')
            fputc('\\', file);
        /* JSON strings cannot hold raw control characters. */
        if (*p < ' ')
            fprintf(file, "\\u%04x", *p);
        else
            fputc(*p, file);
    }
    fputc('"', file);
}

int main(int argc, char **argv)
{
    if (argc != 3)
        return 2;
    FILE *file = fopen(argv[1], "wb");
    if (!file)
        return 1;
    fputs("{\n  \"provider\": ", file);
    string(file, argv[2]);
    fputs(",\n  \"version\": ", file);
    string(file, av_version_info());
    fputs(",\n  \"license\": ", file);
    string(file, avcodec_license());
    fputs(",\n  \"configuration\": ", file);
    string(file, avcodec_configuration());
    fprintf(file, ",\n  \"avcodecVersion\": %u,\n  \"avformatVersion\": %u\n}\n", avcodec_version(),
            avformat_version());
    return fclose(file) != 0;
}

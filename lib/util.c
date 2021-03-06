
#include "main.h"
#include <libgen.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
int check_file_exist(const char *storage) {
    return access(storage, F_OK) != -1;
}

int check_file_size(const char *storage) {
    struct stat st;
    stat(storage, &st);
    return st.st_size;
}

int check_basedir_exist(const char *storage) {
    char *_storage = strdup(storage);
    char *basedir = dirname(_storage);

    struct stat d; int ret = 0;
    if (stat(basedir, &d) != 0 || !S_ISDIR(d.st_mode))
        ret = -1;
    free(_storage);
    return ret;
}

enum CompressionType get_compression(const char *flag) {
    if (flag == NULL) {
        return COMPRESS_NONE;
    } else if (!strcmp(flag, "zstd") || !strcmp(flag, "zstandard")) {
        return COMPRESS_ZSTD;
    } else if (!strcmp(flag, "lz4")) {
        return COMPRESS_LZ4;
    } else {
        FATAL("Unknown compression algorithm: %s\n", flag);
        exit(1);
    }

    return 0;
}

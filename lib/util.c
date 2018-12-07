
#include "main.h"
#include <libgen.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
int check_file_exist(const char *storage) {
    return access(storage, F_OK) != -1;
}

int check_basedir_exist(const char *storage) {
    char *_storage = strdup(storage);
    char *basedir = dirname(_storage);
    free(_storage);

    struct stat d;
    if (stat(basedir, &d) != 0 || !S_ISDIR(d.st_mode)) {
        return -1;
    }
    return 0;
}

enum CompressionType get_compression(const char *flag) {
    if (flag == NULL) {
        return COMPRESS_NONE;
    } else if (!strcmp(flag, "zstd") || !strcmp(flag, "zstandard")) {
        return COMPRESS_ZSTD;
    } else if (!strcmp(flag, "lz4")) {
        return COMPRESS_LZ4;
    } else {
        fprintf(stderr, "Unknown compression algorithm: %s\n", flag);
        return 0;
    }
}

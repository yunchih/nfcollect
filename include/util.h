#ifndef UTIL_H
#define UTIL_H
#include "main.h"
int check_basedir_exist(const char *storage);
int check_file_size(const char *storage);
int check_file_exist(const char *storage);
enum CompressionType get_compression(const char *flag);

#endif // UTIL_H

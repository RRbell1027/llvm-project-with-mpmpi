#pragma once
#include <cstdio>

#define MESSAGE0(msg, ...) fprintf(stderr, msg "\n", ##__VA_ARGS__)
#pragma once
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wbitwise-instead-of-logical"
#pragma clang diagnostic ignored "-Wdeprecated-enum-enum-conversion"
#include "asmjit/asmjit.h"
#pragma clang diagnostic pop
#else
#include "asmjit/asmjit.h"
#endif

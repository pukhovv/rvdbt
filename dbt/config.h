#pragma once

// TODO: verify while elf loading
#define CONFIG_ZERO_MMU_BASE

// #define CONFIG_USE_STATEMAPS // TODO: Re-enable or drop after next tier introduction

// #define LOG_TRACE_ENABLE

#if defined(LOG_TRACE_ENABLE) && !defined(NDEBUG)
#define log_trace() Logger("[TRACE]: ")
#else
#define log_trace() NullLogger()
#endif
#ifndef NDEBUG
#define log_bt()      Logger("[BT]:      ")
#define log_cflow()   Logger("[CFLOW]:   ")
#define log_ukernel() Logger("[UKERNEL]: ")
#else
#define log_bt() NullLogger()
#define log_cflow() NullLogger()
#define log_ukernel() NullLogger()
#endif

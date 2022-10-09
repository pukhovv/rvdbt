#pragma once

#define CONFIG_ZERO_MMU_BASE // TODO: verify while elf loading

//#define CONFIG_USE_INTERP

//#define CONFIG_DUMP_TRACE

// #define CONFIG_USE_STATEMAPS // TODO: Re-enable or drop after next tier introduction

/****************************************************************************/

namespace config
{
#ifdef CONFIG_USE_INTERP
static constexpr bool use_interp = true;
#else
static constexpr bool use_interp = false;
#endif
} // namespace config

#if defined(CONFIG_DUMP_TRACE) && !defined(NDEBUG)
#define log_trace() Logger("[TRACE]: ")
#else
#define log_trace() NullLogger()
#endif
#ifndef NDEBUG
#define log_bt() Logger("[BT]:      ")
#define log_cflow() Logger("[CFLOW]:   ")
#define log_ukernel() Logger("[UKERNEL]: ")
#else
#define log_bt() NullLogger()
#define log_cflow() NullLogger()
#define log_ukernel() NullLogger()
#endif

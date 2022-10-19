#pragma once

#define CONFIG_LINUX_GUEST

//#define CONFIG_ZERO_MMU_BASE // TODO: verify while elf loading

#define CONFIG_USE_INTERP
#define CONFIG_DUMP_TRACE
//#define CONFIG_DUMP_TRACE_VERBOSE

//#define CONFIG_USE_STATEMAPS // TODO: Re-enable or drop after next tier introduction

/****************************************************************************/

namespace config
{
#ifdef CONFIG_USE_INTERP
static constexpr bool use_interp = true;
#else
static constexpr bool use_interp = false;
#endif
#ifdef CONFIG_DUMP_TRACE
static constexpr bool dump_trace = true;
#else
static constexpr bool dump_trace = false;
#endif
} // namespace config

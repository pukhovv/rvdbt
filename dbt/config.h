#pragma once

#define CONFIG_LINUX_GUEST

#if !(__has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__))
#define CONFIG_ZERO_MMU_BASE
#endif

#define CONFIG_UNSAFE_TRAPS

/****************************************************************************/

#define CONFIG_USE_INTERP

#ifndef NDEBUG
//#define CONFIG_DUMP_TRACE
//#define CONFIG_DUMP_TRACE_VERBOSE
#endif

/****************************************************************************/

namespace config
{
#ifdef CONFIG_USE_INTERP
static constexpr bool use_interp = true;
#else
static constexpr bool use_interp = false;
#endif
#ifdef CONFIG_UNSAFE_TRAPS
static constexpr bool unsafe_traps = true;
#else
static constexpr bool unsafe_traps = false;
#endif
#ifdef CONFIG_DUMP_TRACE
static constexpr bool dump_trace = true;
#else
static constexpr bool dump_trace = false;
#endif
} // namespace config

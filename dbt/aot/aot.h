#pragma once

#include "dbt/aot/aot_module.h"
#include "dbt/qmc/compile.h"
#include "dbt/tcache/objprof.h"
#include "dbt/ukernel.h"

#include <sstream>

namespace dbt
{

static constexpr char const *AOT_O_EXTENSION = ".aot.o";
static constexpr char const *AOT_SO_EXTENSION = ".aot.so";
static constexpr char const *AOT_SYM_AOTTAB = "_aot_tab";
static constexpr std::string_view AOT_SYM_PREFIX = "_x";

inline std::string MakeAotSymbol(u32 ip)
{
	std::stringstream ss;
	ss << AOT_SYM_PREFIX << std::hex << ip;
	return ss.str();
}

struct AOTSymbol {
	u32 gip;
	u64 aot_vaddr;
};

struct AOTTabHeader {
	u64 n_sym;
	AOTSymbol sym[];
};

void AOTCompileELF();
void LLVMAOTCompileELF();
void BootAOTFile();

ModuleGraph BuildModuleGraph(objprof::PageData const &page);
void ExecuteAOTLinker(std::vector<AOTSymbol> &aot_symbols);

void AOTCompileObject(CompilerRuntime *aotrt);

} // namespace dbt

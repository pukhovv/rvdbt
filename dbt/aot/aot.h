#pragma once

#include "dbt/aot/aot_module.h"
#include "dbt/qmc/compile.h"
#include "dbt/tcache/objprof.h"

#include <sstream>

namespace dbt
{

void AOTCompileELF();
void LLVMAOTCompileELF();
void BootAOTFile();

static constexpr char const *AOT_O_EXTENSION = ".aot.o";
static constexpr char const *AOT_SO_EXTENSION = ".aot.so";
static constexpr char const *AOT_SYM_AOTTAB = "_aot_tab";

struct AOTSymbol {
	u32 gip;
	u64 aot_vaddr;
};

struct AOTTabHeader {
	u64 n_sym;
	AOTSymbol sym[];
};

ModuleGraph BuildModuleGraph(objprof::PageData const &page);
void LinkAOTObject(std::vector<AOTSymbol> &aot_symbols);

void AOTCompileObject(CompilerRuntime *aotrt);

void ProcessLLVMStackmaps(std::vector<AOTSymbol> &aot_symbols);

} // namespace dbt

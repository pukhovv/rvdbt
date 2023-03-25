#pragma once

#include "dbt/qmc/compile.h"
#include "dbt/ukernel.h"

namespace dbt
{

static constexpr char const *AOT_O_EXTENSION = ".aot.o";
static constexpr char const *AOT_SO_EXTENSION = ".aot.so";
static constexpr char const *AOT_SYM_AOTTAB = "_aot_tab";
static constexpr std::string_view AOT_SYM_PREFIX = "_x";

struct AOTSymbol {
	u32 gip;
	u64 aot_vaddr;
};

struct AOTTabHeader {
	u64 n_sym;
	AOTSymbol sym[];
};

void AOTCompileELF();
void BootAOTFile();

void AOTCompileObject(CompilerRuntime *aotrt);

} // namespace dbt

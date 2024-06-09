#include "dbt/aot/aot.h"
#include "dbt/qmc/compile.h"
#include "dbt/tcache/objprof.h"
#include "dbt/util/fsmanager.h"

#include <sstream>

extern "C" {
#include <dlfcn.h>
#include <fcntl.h>
#include <link.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
}

namespace dbt
{
LOG_STREAM(aot)

void BootAOTFile()
{
	void *so_handle;
	link_map *lmap;
	{
		DBT_FS_LOCK();
		auto aot_path = objprof::GetCachePath(AOT_SO_EXTENSION);

		if (so_handle = dlopen(aot_path.c_str(), RTLD_NOW); !so_handle) {
			log_aot("failed to open %s: %s, skip aot boot", aot_path.c_str(), dlerror());
		}
		if (dlinfo(so_handle, RTLD_DI_LINKMAP, (void *)&lmap) < 0) {
			Panic();
		}
	}

	auto l_addr = (u8 *)lmap->l_addr;

	auto aottab = (AOTTabHeader const *)dlsym(so_handle, AOT_SYM_AOTTAB);
	assert(aottab);

	auto announce = [l_addr](AOTSymbol const *sym) {
		auto tb = tcache::AllocateTBlock();
		if (tb == nullptr) {
			Panic();
		}
		tb->ip = sym->gip;
		tb->tcode = TBlock::TCode{l_addr + sym->aot_vaddr, 0};
		tcache::Insert(tb);
	};

	for (u64 idx = 0; idx < aottab->n_sym; ++idx) {
		announce(&aottab->sym[idx]);
	}
}

} // namespace dbt

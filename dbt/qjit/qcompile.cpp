#include "dbt/qjit/qcompile.h"
#include "dbt/guest/rv32_qir.h"
#include "dbt/qjit/qcg/qcg.h"
#include "dbt/qjit/qir_printer.h"

namespace dbt::qir
{

TBlock *CompileAt(u32 ip)
{
	MemArena arena(1024 * 64);
	qir::Region region(&arena, IRTranslator::state_info);

	u32 upper_bound = -1;
#ifndef CONFIG_DUMP_TRACE
	if (auto *tb_bound = tcache::LookupUpperBound(ip)) {
		upper_bound = tb_bound->ip;
	}
#endif

	IRTranslator::Translate(&region, ip, upper_bound);

	PrinterPass printer;
	printer.run(&region);

	auto tc = qcg::Generate(&region, ip);

	// TODO: concurrent tcache
	auto tb = tcache::AllocateTBlock();
	if (tb == nullptr) {
		Panic();
	}
	tb->ip = ip;
	tb->tcode = tc;
	tcache::Insert(tb);

	return tb;
}

} // namespace dbt::qir

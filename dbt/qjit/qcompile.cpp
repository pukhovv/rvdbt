#include "dbt/qjit/qcompile.h"
#include "dbt/guest/rv32_qir.h"
#include "dbt/qjit/qcg.h"
#include "dbt/qjit/qir_printer.h"

namespace dbt::qir
{

TBlock *Translate(u32 ip)
{
	MemArena arena(1024 * 64);
	qir::Region region(&arena, IRTranslator::state_info);

	u32 upper_bound = -1;
#ifndef CONFIG_DUMP_TRACE
	if (auto *tb_bound = tcache::LookupUpperBound(ip)) {
		upper_bound = tb_bound->ip;
	}
#endif

	log_qir("IRTranslator Translate [%08x]", ip);
	IRTranslator::Translate(&region, ip, upper_bound);
	log_qir("IRTranslator Translated", ip);

	PrinterPass printer;
	printer.run(&region);

	return qcg::QCodegen::Generate(&region, ip);
}

} // namespace dbt::qir

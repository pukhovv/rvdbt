#include "dbt/aot/aot_module.h"

namespace dbt
{

void ModuleGraph::Dump()
{
	auto dump_node = [](ModuleGraphNode const &n) {
		if (n.flags.is_segment_entry) {
			log_modulegraph("B%08x[fillcolor=green]", n.ip);
		} else if (n.flags.is_brind_target) {
			log_modulegraph("B%08x[fillcolor=orange]", n.ip);
		} else {
			log_modulegraph("B%08x[fillcolor=cyan]", n.ip);
		}
	};

	auto dump_edge = [](ModuleGraphNode const &n, ModuleGraphNode const &t) {
		if (n.ip >= t.ip) {
			log_modulegraph("B%08x->B%08x[color=red,penwidth=2,dir=back]", n.ip, t.ip);
		} else {
			log_modulegraph("B%08x->B%08x", n.ip, t.ip);
		}
	};

	for (auto const &it : ip_map) {
		auto const &n = *it.second;
		dump_node(n);
		for (auto const &s : n.succs) {
			dump_edge(n, *s);
		}
	}
}

} // namespace dbt

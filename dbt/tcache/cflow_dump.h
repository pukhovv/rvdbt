#pragma once

#include "dbt/mmu.h"
#include "dbt/util/logger.h"

namespace dbt
{
LOG_STREAM(cflow);

struct cflow_dump {
	static inline void RecordEntry(u32 ip)
	{
		log_cflow("B%08x[fillcolor=cyan]", ip);
	}

	static inline void RecordBrindEntry(u32 ip)
	{
		log_cflow("B%08x[fillcolor=orange]", ip);
	}

	static inline void RecordGBr(u32 ip, u32 tgtip)
	{
		if (rounddown(ip, mmu::PAGE_SIZE) != rounddown(tgtip, mmu::PAGE_SIZE)) {
			log_cflow("B%08x->B%08x[color=seagreen,penwidth=2]", ip, tgtip);
		} else if (ip >= tgtip) {
			log_cflow("B%08x->B%08x[color=red,penwidth=2,dir=back]", tgtip, ip);
		} else {
			log_cflow("B%08x->B%08x", ip, tgtip);
		}
	}

	static inline void RecordGBrLink(u32 ip, u32 tgtip, u32 ip_link)
	{
		log_cflow("B%08x->B%08x[color=blue,penwidth=2]"
			  "B%08x->B%08x[color=gray,penwidth=2,style=dashed]",
			  ip, tgtip, ip, ip_link);
	}

	static inline void RecordGBrind(u32 ip, u32 ip_link = 0)
	{
		log_cflow("B%08x_brind[fillcolor=purple,style=invis];"
			  "B%08x->B%08x_brind[color=purple,penwidth=3,arrowhead=dot]",
			  ip, ip, ip);
		if (ip_link) {
			log_cflow("B%08x->B%08x[color=gray,penwidth=2,style=dashed]", ip, ip_link);
		}
	}
};

} // namespace dbt

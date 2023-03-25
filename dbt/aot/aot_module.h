#pragma once

#include "dbt/arena.h"
#include "dbt/mmu.h"
#include "dbt/tcache/cflow_dump.h"
#include <list>
#include <map>
#include <memory>

namespace dbt
{
LOG_STREAM(modulegraph)

struct ModuleGraphNode {
	explicit ModuleGraphNode(u32 ip_) : ip(ip_) {}

	struct EdgeInfo {
		enum class Type {
			BR,
			CALL,
		};
	};

	void AddSucc(ModuleGraphNode *succ)
	{
		succs.push_back(succ);
		succ->preds.push_back(this);
	}

	u32 ip{};
	u32 ip_end{0};
	bool brind_target{false};

	std::list<ModuleGraphNode *> succs;
	std::list<ModuleGraphNode *> preds;
};

struct ModuleGraph {
	explicit ModuleGraph(u32 page_ip_) : page_ip(page_ip_) {}

	inline bool InModule(u32 ip)
	{
		return page_ip == rounddown(ip, mmu::PAGE_SIZE);
	}

	inline ModuleGraphNode *GetNode(u32 ip)
	{
		if (!InModule(ip)) {
			return nullptr;
		}
		auto it = ip_map.find(ip);
		if (likely(it != ip_map.end())) {
			return it->second.get();
		}
		return nullptr;
	}

	inline ModuleGraphNode *AddNode(u32 ip)
	{
		auto res = ip_map.insert({ip, std::make_unique<ModuleGraphNode>(ip)});
		assert(res.second);
		return res.first->second.get();
	}

	inline void RecordEntry(u32 ip)
	{
		cflow_dump::RecordEntry(ip);
		AddNode(ip);
	}

	inline void RecordBrindEntry(u32 ip)
	{
		cflow_dump::RecordBrindEntry(ip);
		GetNode(ip)->brind_target = true;
	}

	inline void RecordGBr(u32 ip, u32 tgtip)
	{
		cflow_dump::RecordGBr(ip, tgtip);
		if (auto tgt = GetNode(tgtip); tgt) {
			GetNode(ip)->AddSucc(tgt);
		} else {
			// sidecall
		}
	}

	inline void RecordGBrLink(u32 ip, u32 tgtip, u32 ip_link)
	{
		cflow_dump::RecordGBrLink(ip, tgtip, ip_link);
		if (auto tgt = GetNode(tgtip); tgt) {
			GetNode(ip)->AddSucc(tgt);
		} else {
			// sidecall
		}
	}

	inline void RecordGBrind(u32 ip, u32 ip_link = 0)
	{
		cflow_dump::RecordGBrind(ip, ip_link);
		// sidecall
	}

	void Dump();

	u32 const page_ip;

	using RegionMap = std::map<u32, std::unique_ptr<ModuleGraphNode>>;
	RegionMap ip_map;
};

} // namespace dbt

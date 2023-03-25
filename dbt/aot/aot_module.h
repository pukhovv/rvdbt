#pragma once

#include "dbt/qmc/compile.h"
#include "dbt/qmc/marker.h"
#include "dbt/util/logger.h"
#include <map>
#include <memory>
#include <set>
#include <vector>

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
	struct {
		bool is_brind_target : 1 {false};
		bool is_segment_entry : 1 {false};
		bool region_entry : 1 {false};
	} flags;

	std::vector<ModuleGraphNode *> succs;
	std::vector<ModuleGraphNode *> preds;

	ModuleGraphNode *dominator{};
	std::set<ModuleGraphNode *> domfrontier;
	qir::Mark mark{};

	qir::Mark GetMark() const
	{
		return mark;
	}
	void SetMark(qir::Mark mark_)
	{
		mark = mark_;
	}
};

struct ModuleGraph {
	explicit ModuleGraph(qir::CodeSegment segment_)
	    : segment(segment_), root(std::make_unique<ModuleGraphNode>(0))
	{
	}

	inline bool InModule(u32 ip)
	{
		return segment.InSegment(ip);
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
		AddNode(ip);
	}

	inline void RecordBrindTarget(u32 ip)
	{
		auto *node = GetNode(ip);
		node->flags.is_brind_target = true;
		root->AddSucc(node);
	}

	inline void RecordSegmentEntry(u32 ip)
	{
		auto *node = GetNode(ip);
		node->flags.is_segment_entry = true;
		root->AddSucc(node);
	}

	inline void RecordGBr(u32 ip, u32 tgtip)
	{
		if (auto tgt = GetNode(tgtip); tgt) {
			GetNode(ip)->AddSucc(tgt);
		} else {
			// sidecall
		}
	}

	inline void RecordGBrLink(u32 ip, u32 tgtip, u32 ip_link)
	{
		if (auto tgt = GetNode(tgtip); tgt) {
			GetNode(ip)->AddSucc(tgt);
		} else {
			// sidecall
		}
	}

	inline void RecordGBrind(u32 ip, u32 ip_link = 0)
	{
		// sidecall
	}

	void ComputeDomTree();
	void ComputeDomFrontier();
	void ComputeRegionIDF();
	std::vector<std::vector<ModuleGraphNode *>> ComputeRegionDomSets();

	std::vector<std::vector<ModuleGraphNode *>> ComputeRegions();
	void DumpRegions(std::vector<std::vector<ModuleGraphNode *>> const &regions);
	void Dump();

	qir::CodeSegment segment;

	std::unique_ptr<ModuleGraphNode> root;

	using RegionMap = std::map<u32, std::unique_ptr<ModuleGraphNode>>;
	RegionMap ip_map;

	qir::MarkerKeeper markers;
};

} // namespace dbt

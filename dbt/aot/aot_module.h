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

// TODO: optimize layout and use Arena containers
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
		bool is_brind_source : 1 {false};
		bool is_crosssegment_br : 1 {false};
	} flags;

	ModuleGraphNode *link{};

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

	bool InModule(u32 ip)
	{
		return segment.InSegment(ip);
	}

	ModuleGraphNode *GetNode(u32 ip)
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

	ModuleGraphNode *AddNode(u32 ip)
	{
		auto res = ip_map.insert({ip, std::make_unique<ModuleGraphNode>(ip)});
		assert(res.second);
		return res.first->second.get();
	}

	void RecordEntry(u32 ip)
	{
		AddNode(ip);
	}

	void RecordBrindTarget(u32 ip)
	{
		auto *node = GetNode(ip);
		node->flags.is_brind_target = true;
		root->AddSucc(node);
	}

	void RecordSegmentEntry(u32 ip)
	{
		auto *node = GetNode(ip);
		node->flags.is_segment_entry = true;
		root->AddSucc(node);
	}

	void RecordGBr(u32 ip, u32 tgtip)
	{
		auto src = GetNode(ip);
		if (auto tgt = GetNode(tgtip); tgt) {
			src->AddSucc(tgt);
		} else {
			src->flags.is_crosssegment_br = true;
		}
	}

	void RecordGBrind(u32 ip)
	{
		GetNode(ip)->flags.is_brind_source = true;
	}

	void RecordLink(u32 ip, u32 linkip)
	{
		GetNode(ip)->link = GetNode(linkip);
	}

	void ComputeDomTree();
	void ComputeDomFrontier();
	void ComputeRegionIDF();
	std::vector<std::vector<ModuleGraphNode *>> ComputeRegionDomSets();

	std::vector<std::vector<ModuleGraphNode *>> ComputeRegions();
	void Dump(FILE *f, std::vector<std::vector<ModuleGraphNode *>> const *regions = nullptr);

	qir::CodeSegment segment;

	std::unique_ptr<ModuleGraphNode> root;

	using RegionMap = std::map<u32, std::unique_ptr<ModuleGraphNode>>;
	RegionMap ip_map;

	qir::MarkerKeeper markers;
};

void InitModuleGraphDump(char const *dir);

} // namespace dbt

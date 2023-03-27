#pragma once

#include "dbt/arena.h"
#include "dbt/mmu.h"
#include "dbt/qmc/compile.h"
#include "dbt/tcache/cflow_dump.h"
#include <list>
#include <map>
#include <memory>
#include <set>

namespace dbt
{
LOG_STREAM(modulegraph)

using Mark = uint16_t;

struct MarkerKeeper;

template <typename N, typename S>
struct Marker {
	inline Marker(MarkerKeeper *mkeeper, u8 states);

	inline S Get(N *n)
	{
		Mark m = n->GetMark();
		assert(m < mmax);
		if (m < mmin) {
			return 0;
		}
		return static_cast<S>(m - mmin);
	}

	inline void Set(N *n, S s)
	{
		auto m = static_cast<Mark>(s);
		assert(n->GetMark() < mmax);
		assert(m + mmin < mmax);
		n->SetMark(m + mmin);
	}

private:
	Marker() = default;
	Mark mmin{0}, mmax{0};
};

struct MarkerKeeper {

private:
	template <typename N, typename S>
	friend struct Marker;

	Mark mmin{0}, mmax{0};
};

template <typename N, typename S>
inline Marker<N, S>::Marker(MarkerKeeper *keeper, u8 states)
{
	mmin = keeper->mmin = keeper->mmax;
	mmax = keeper->mmax = keeper->mmin + states;
}

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

	std::list<ModuleGraphNode *> succs;
	std::list<ModuleGraphNode *> preds;

	ModuleGraphNode *dominator{};
	std::set<ModuleGraphNode *> domfrontier;
	Mark mark{};

	Mark GetMark() const
	{
		return mark;
	}
	void SetMark(Mark mark_)
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
		cflow_dump::RecordEntry(ip);
		AddNode(ip);
	}

	inline void RecordBrindTarget(u32 ip)
	{
		cflow_dump::RecordBrindEntry(ip);
		auto *node = GetNode(ip);
		node->flags.is_brind_target = true;
		root->AddSucc(node);
	}

	inline void RecordSegmentEntry(u32 ip)
	{
		cflow_dump::RecordBrindEntry(ip);
		auto *node = GetNode(ip);
		node->flags.is_segment_entry = true;
		root->AddSucc(node);
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

	void ComputeDomTree();
	void ComputeDomFrontier();
	void MergeRegions();
	void Dump();

	qir::CodeSegment segment;

	std::unique_ptr<ModuleGraphNode> root;

	using RegionMap = std::map<u32, std::unique_ptr<ModuleGraphNode>>;
	RegionMap ip_map;

	MarkerKeeper markers;
};

} // namespace dbt

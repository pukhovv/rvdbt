#include "dbt/aot/aot_module.h"
#include <iomanip>
#include <set>
#include <sstream>
#include <vector>

namespace dbt
{
LOG_STREAM(aot)

void ModuleGraph::Dump()
{
	auto dump_node = [](ModuleGraphNode const &n) {
		if (n.flags.is_segment_entry) {
			log_modulegraph("B%08x[fillcolor=green]", n.ip);
		} else if (n.flags.is_brind_target) {
			log_modulegraph("B%08x[fillcolor=orange]", n.ip);
		} else if (n.flags.region_entry) {
			log_modulegraph("B%08x[fillcolor=purple]", n.ip);
		} else {
			log_modulegraph("B%08x[fillcolor=cyan]", n.ip);
		}
	};

	auto dump_edge = [](ModuleGraphNode const &n, ModuleGraphNode const &t) {
		if (n.ip >= t.ip) {
			log_modulegraph("B%08x->B%08x[color=red,penwidth=2,dir=back]", t.ip, n.ip);
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
		// if (n.dominator && n.dominator != root.get()) {
		// 	log_modulegraph("B%08x->B%08x[color=grey]", n.dominator->ip, n.ip);
		// }
	}
}

void ModuleGraph::DumpRegions(std::vector<std::vector<ModuleGraphNode *>> const &regions)
{
	for (auto const &r : regions) {
		std::stringstream ss;
		ss << std::hex;
		ss << "subgraph cluster_R" << std::setfill('0') << std::setw(8) << r[0]->ip
		   << "{style=filled;color=lightgrey;";
		for (size_t i = 0; i < r.size(); ++i) {
			ss << " B" << std::setfill('0') << std::setw(8) << r[i]->ip;
		}
		ss << "}";
		auto str = ss.str();
		log_modulegraph(str.c_str());
	}
}

struct RPOTraversal {
	using OrderVec = std::vector<ModuleGraphNode *>;

	using rpo_iterator = OrderVec::reverse_iterator;
	using const_rpo_iterator = OrderVec::const_reverse_iterator;

	RPOTraversal(ModuleGraph &graph)
	{
		Compute(graph);
	}

	rpo_iterator begin()
	{
		return po_nodes.rbegin();
	}
	const_rpo_iterator begin() const
	{
		return po_nodes.crbegin();
	}
	rpo_iterator end()
	{
		return po_nodes.rend();
	}
	const_rpo_iterator end() const
	{
		return po_nodes.crend();
	}

private:
	void Compute(ModuleGraph &graph);

	std::vector<ModuleGraphNode *> po_nodes;
};

void RPOTraversal::Compute(ModuleGraph &graph)
{
	auto n_nodes = graph.ip_map.size() + 1;
	po_nodes.reserve(n_nodes);

	using ChildIt = std::list<ModuleGraphNode *>::iterator;
	std::vector<std::pair<ModuleGraphNode *, ChildIt>> stk;

	Marker<ModuleGraphNode, bool> marker(&graph.markers, 2);

	auto push_node = [&](ModuleGraphNode *node) {
		if (!marker.Get(node)) {
			stk.push_back(std::make_pair(node, node->succs.begin()));
			marker.Set(node, true);
		}
	};

	push_node(graph.root.get());

	while (!stk.empty()) {
		auto &p = stk.back();
		if (p.second == p.first->succs.end()) {
			po_nodes.push_back(p.first);
			stk.pop_back();
		} else {
			push_node(*p.second++);
		}
	}
}

void ModuleGraph::ComputeDomTree()
{
	using Node = ModuleGraphNode;
	auto const rpot = RPOTraversal(*this);

	auto n_nodes = ip_map.size() + 1;
	Marker<ModuleGraphNode, u16> rpon(&markers, n_nodes);
	{
		u16 rpo_no = 0;
		for (auto n : rpot) {
			rpon.Set(n, rpo_no++);
		}
		if (rpo_no != n_nodes) {
			Panic("unreachable regions in modulegraph");
		}
	}

	auto *start = root.get();
	start->dominator = start;

	auto intersect = [&](Node *b1, Node *b2) {
		while (b1 != b2) {
			while (rpon.Get(b1) > rpon.Get(b2)) {
				b1 = b1->dominator;
			}
			while (rpon.Get(b2) > rpon.Get(b1)) {
				b2 = b2->dominator;
			}
		}
		return b1;
	};

	bool changed = true;
	while (changed) {
		changed = false;
		for (auto b : rpot) {
			if (b == start) {
				continue;
			}
			Node *new_idom = nullptr;
			for (auto p : b->preds) {
				if (p->dominator) {
					new_idom = new_idom ? intersect(p, new_idom) : p;
				}
			}
			assert(new_idom);
			if (b->dominator != new_idom) {
				b->dominator = new_idom;
				changed = true;
			}
		}
	}
}

void ModuleGraph::ComputeDomFrontier()
{
	for (auto const &e : ip_map) {
		auto b = e.second.get();
		if (b->preds.size() > 1) {
			for (auto runner : b->preds) {
				while (runner != b->dominator) {
					runner->domfrontier.insert(b);
					runner = runner->dominator;
				}
			}
		}
	}
}

void ModuleGraph::ComputeRegionIDF()
{
	std::vector<ModuleGraphNode *> wlist;

	for (auto const &e : ip_map) {
		auto n = e.second.get();
		if (n->flags.is_brind_target || n->flags.is_segment_entry) {
			n->flags.region_entry = true;
			wlist.push_back(n);
		}
	}

	while (!wlist.empty()) {
		auto x = wlist.back();
		wlist.pop_back();

		for (auto y : x->domfrontier) {
			if (!y->flags.region_entry) {
				wlist.push_back(y);
			}
		}

		x->flags.region_entry = true;
	}
}

std::vector<std::vector<ModuleGraphNode *>> ModuleGraph::ComputeRegionDomSets()
{
	std::vector<std::vector<ModuleGraphNode *>> regions;

	auto compute_region = [this](ModuleGraphNode *entry) {
		Marker<ModuleGraphNode, bool> marker(&markers, 2);

		std::vector<ModuleGraphNode *> region_nodes{};

		using ChildIt = std::list<ModuleGraphNode *>::iterator;
		std::vector<std::pair<ModuleGraphNode *, ChildIt>> stk;

		auto push_node = [&](ModuleGraphNode *node, bool force = false) {
			if (force || (!marker.Get(node) && !node->flags.region_entry)) {
				stk.push_back(std::make_pair(node, node->succs.begin()));
				marker.Set(node, true);
				region_nodes.push_back(node);
			}
		};

		push_node(entry, true);

		while (!stk.empty()) {
			auto &p = stk.back();
			if (p.second == p.first->succs.end()) {
				stk.pop_back();
			} else {
				push_node(*p.second++);
			}
		}

		return region_nodes;
	};

	for (auto const &e : ip_map) {
		auto n = e.second.get();
		if (n->flags.region_entry) {
			regions.push_back(compute_region(n));
		}
	}

	return regions;
}

std::vector<std::vector<ModuleGraphNode *>> ModuleGraph::ComputeRegions()
{
	ComputeDomTree();
	ComputeDomFrontier();
	ComputeRegionIDF();
	auto regions = ComputeRegionDomSets();

	Dump();
	DumpRegions(regions);

	return regions;
}

} // namespace dbt

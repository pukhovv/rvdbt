#include "dbt/aot/aot_module.h"
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
				if (!new_idom) {
					new_idom = p;
				}
				if (p->dominator) {
					new_idom = intersect(p, new_idom);
				}
			}
			if (b->dominator != new_idom) {
				b->dominator = new_idom;
				changed = true;
			}
		}
	}
}

void ModuleGraph::MergeRegions()
{
	ComputeDomTree();
}

} // namespace dbt

// SPDX-License-Identifier: GPL-2.0
/*
 * CXL Fabric Interconnect - Routing Engine
 *
 * Copyright (C) 2025 InterMatrix Project
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/cxl/cxl_fabric.h>

/* Dijkstra's algorithm for shortest path routing */

#define INFINITY_COST U32_MAX

struct routing_vertex {
	u32 cost;
	u32 prev;
	bool visited;
	void *entity;  /* Switch or endpoint */
	enum { VERTEX_SWITCH, VERTEX_ENDPOINT } type;
};

/*
 * Build topology graph for routing
 */
static int build_topology_graph(struct cxl_fabric *fabric,
				struct routing_vertex **vertices_out,
				u32 *num_vertices_out)
{
	struct routing_vertex *vertices;
	struct cxl_switch *sw;
	struct cxl_endpoint *ep;
	u32 num_vertices;
	u32 idx = 0;

	num_vertices = fabric->num_switches + fabric->num_endpoints;
	if (num_vertices == 0)
		return -EINVAL;

	vertices = kcalloc(num_vertices, sizeof(*vertices), GFP_KERNEL);
	if (!vertices)
		return -ENOMEM;

	/* Add switches to vertex list */
	list_for_each_entry(sw, &fabric->switches, fabric_node) {
		vertices[idx].entity = sw;
		vertices[idx].type = VERTEX_SWITCH;
		vertices[idx].cost = INFINITY_COST;
		vertices[idx].visited = false;
		idx++;
	}

	/* Add endpoints to vertex list */
	list_for_each_entry(ep, &fabric->endpoints, fabric_node) {
		vertices[idx].entity = ep;
		vertices[idx].type = VERTEX_ENDPOINT;
		vertices[idx].cost = INFINITY_COST;
		vertices[idx].visited = false;
		idx++;
	}

	*vertices_out = vertices;
	*num_vertices_out = num_vertices;

	return 0;
}

/*
 * Find vertex index for endpoint
 */
static int find_endpoint_vertex(struct routing_vertex *vertices, u32 num_vertices,
				struct cxl_endpoint *ep)
{
	u32 i;

	for (i = 0; i < num_vertices; i++) {
		if (vertices[i].type == VERTEX_ENDPOINT &&
		    vertices[i].entity == ep)
			return i;
	}

	return -1;
}

/*
 * Find vertex with minimum cost
 */
static u32 find_min_cost_vertex(struct routing_vertex *vertices, u32 num_vertices)
{
	u32 i, min_idx = U32_MAX;
	u32 min_cost = INFINITY_COST;

	for (i = 0; i < num_vertices; i++) {
		if (!vertices[i].visited && vertices[i].cost < min_cost) {
			min_cost = vertices[i].cost;
			min_idx = i;
		}
	}

	return min_idx;
}

/*
 * Get edge cost between two vertices
 */
static u32 get_edge_cost(struct routing_vertex *from, struct routing_vertex *to)
{
	struct cxl_switch *sw_from, *sw_to;
	struct cxl_endpoint *ep;
	struct cxl_port *port;
	int i;

	/* Switch to Switch */
	if (from->type == VERTEX_SWITCH && to->type == VERTEX_SWITCH) {
		sw_from = from->entity;
		sw_to = to->entity;

		/* Check if directly connected */
		for (i = 0; i < sw_from->num_ports; i++) {
			port = &sw_from->ports[i];
			if (port->state == CXL_PORT_UP &&
			    port->child_switch == sw_to) {
				return port->link.latency_ns;
			}
		}
	}

	/* Switch to Endpoint */
	if (from->type == VERTEX_SWITCH && to->type == VERTEX_ENDPOINT) {
		sw_from = from->entity;
		ep = to->entity;

		/* Check if endpoint connected to this switch */
		if (ep->fabric_port && ep->fabric_port->parent_switch == sw_from) {
			return ep->fabric_port->link.latency_ns;
		}
	}

	/* Endpoint to Switch */
	if (from->type == VERTEX_ENDPOINT && to->type == VERTEX_SWITCH) {
		ep = from->entity;
		sw_to = to->entity;

		if (ep->fabric_port && ep->fabric_port->parent_switch == sw_to) {
			return ep->fabric_port->link.latency_ns;
		}
	}

	return INFINITY_COST;
}

/*
 * Compute route using Dijkstra's algorithm
 */
int cxl_route_compute(struct cxl_fabric *fabric,
		      struct cxl_endpoint *src,
		      struct cxl_endpoint *dst,
		      struct cxl_route *route)
{
	struct routing_vertex *vertices = NULL;
	u32 num_vertices = 0;
	int src_idx, dst_idx;
	u32 current;
	int ret = 0;
	u32 i;

	if (!fabric || !src || !dst || !route)
		return -EINVAL;

	if (src == dst)
		return -EINVAL;

	memset(route, 0, sizeof(*route));
	route->src = src;
	route->dst = dst;

	/* Build graph */
	ret = build_topology_graph(fabric, &vertices, &num_vertices);
	if (ret)
		return ret;

	/* Find source and destination indices */
	src_idx = find_endpoint_vertex(vertices, num_vertices, src);
	dst_idx = find_endpoint_vertex(vertices, num_vertices, dst);

	if (src_idx < 0 || dst_idx < 0) {
		ret = -ENODEV;
		goto out;
	}

	/* Initialize source */
	vertices[src_idx].cost = 0;

	/* Dijkstra's algorithm */
	for (i = 0; i < num_vertices; i++) {
		u32 j;

		current = find_min_cost_vertex(vertices, num_vertices);
		if (current == U32_MAX)
			break;

		vertices[current].visited = true;

		if (current == dst_idx)
			break;  /* Found shortest path */

		/* Relax edges */
		for (j = 0; j < num_vertices; j++) {
			u32 edge_cost, new_cost;

			if (vertices[j].visited)
				continue;

			edge_cost = get_edge_cost(&vertices[current], &vertices[j]);
			if (edge_cost == INFINITY_COST)
				continue;

			new_cost = vertices[current].cost + edge_cost;
			if (new_cost < vertices[j].cost) {
				vertices[j].cost = new_cost;
				vertices[j].prev = current;
			}
		}
	}

	/* Check if path found */
	if (vertices[dst_idx].cost == INFINITY_COST) {
		pr_warn("CXL Route: No path from endpoint%u to endpoint%u\n",
			src->endpoint_id, dst->endpoint_id);
		ret = -EHOSTUNREACH;
		goto out;
	}

	/* Reconstruct path */
	route->total_latency_ns = vertices[dst_idx].cost;
	route->hop_count = 0;

	/* Simple path recording (just store latency for now) */
	route->available_bandwidth_gbps = 64;  /* Default */

	pr_debug("CXL Route: Computed route endpoint%u -> endpoint%u (%u hops, %u ns)\n",
		 src->endpoint_id, dst->endpoint_id,
		 route->hop_count, route->total_latency_ns);

out:
	kfree(vertices);
	return ret;
}
EXPORT_SYMBOL_GPL(cxl_route_compute);

/*
 * Program route into fabric switches
 */
int cxl_route_program_fabric(struct cxl_fabric *fabric, struct cxl_route *route)
{
	struct cxl_switch *sw;
	u32 dst_id;
	u16 output_port;
	int ret;

	if (!fabric || !route || !route->dst)
		return -EINVAL;

	dst_id = route->dst->endpoint_id;

	/* For each switch in the fabric, program the route */
	list_for_each_entry(sw, &fabric->switches, fabric_node) {
		struct cxl_endpoint *ep;

		/* Find which port leads to destination */
		output_port = 0;
		list_for_each_entry(ep, &fabric->endpoints, fabric_node) {
			int i;

			if (ep->endpoint_id != dst_id)
				continue;

			/* Find port connected to this endpoint */
			for (i = 0; i < sw->num_ports; i++) {
				if (sw->ports[i].state == CXL_PORT_UP &&
				    sw->ports[i].endpoint == ep) {
					output_port = i;
					break;
				}
			}

			/* If not directly connected, use inter-switch port */
			if (output_port == 0) {
				for (i = 0; i < sw->num_ports; i++) {
					if (sw->ports[i].state == CXL_PORT_UP &&
					    sw->ports[i].child_switch) {
						output_port = i;
						break;
					}
				}
			}
		}

		if (output_port < sw->num_ports && sw->ops && sw->ops->program_route) {
			ret = sw->ops->program_route(sw, dst_id, output_port);
			if (ret) {
				pr_err("CXL Route: Failed to program switch%u\n",
				       sw->switch_id);
				return ret;
			}
		}
	}

	pr_debug("CXL Route: Programmed route to endpoint%u in fabric%u\n",
		 dst_id, fabric->fabric_id);

	return 0;
}
EXPORT_SYMBOL_GPL(cxl_route_program_fabric);

/*
 * Module initialization
 */
static int __init cxl_route_init(void)
{
	pr_info("CXL Routing Engine: Initialized\n");
	return 0;
}

static void __exit cxl_route_exit(void)
{
	pr_info("CXL Routing Engine: Shutdown\n");
}

module_init(cxl_route_init);
module_exit(cxl_route_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("InterMatrix Project");
MODULE_DESCRIPTION("CXL Fabric Interconnect - Routing Engine");

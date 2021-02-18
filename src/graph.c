#include "deadlock/dl.h"
#include "deadlock/graph.h"
#include "sched.h"

#if DEADLOCK_GRAPH_EXPORT

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * dlgraph_dump() writes the graph to a file in a format accepted by the
 * deadlock-graph utility program.
 */
static void dlgraph_dump(struct dlgraph *, const char *prefix);

/*
 * dlgraph_free() destroys and frees a graph.
 */
static void dlgraph_free(struct dlgraph *);

/*
 * write_node_descriptions_reverse() is a helper function to reverse the order
 * of, and write out, the node description linked list.
 */
static void write_node_descriptions_reverse(FILE *, struct dlgraph_node_description *head);

/*
 * See internal.h this is the head of the linked list of static node
 * descriptions set up by DL_TASK_ENTRY() upon first invocation.
 */
_Atomic(struct dlgraph_node_description *) dl_node_description_lst_head = NULL;

/*
 * dl_next_task_id is declared in internal.h to semi-uniquely identify every
 * task created for graphing purposes.
 */
_Thread_local unsigned long dl_next_task_id = 0;

/*
 * MAYBE_GROW_GEOMETRICALLY() is used to generically define geometric growth
 * functions for graph buffers.
 */
#define MAYBE_GROW_GEOMETRICALLY(T) \
	static void                                                          \
	dlgraph_fragment_maybegrow_##T(struct dlgraph_fragment *frag,        \
	                               size_t avail) {                       \
		if (frag->T##_count + avail < frag->T##_size) return;        \
		while (frag->T##_count + avail >= frag->T##_size) {          \
			frag->T##_size = frag->T##_size * 2;                 \
			if (frag->T##_size == 0) {                           \
				frag->T##_size = 1024; /* TODO arbitrary */  \
			}                                                    \
		}                                                            \
		frag->T = realloc(frag->T, sizeof(*frag->T) *                \
		                             frag->T##_size);                \
		if (!frag->T) {                                              \
			perror("dlgraph_fragment_maybegrow failed to grow"); \
			exit(errno);                                         \
		}                                                            \
	}
MAYBE_GROW_GEOMETRICALLY(continuations)
MAYBE_GROW_GEOMETRICALLY(edges)
MAYBE_GROW_GEOMETRICALLY(nodes)
MAYBE_GROW_GEOMETRICALLY(label_buffer)
#undef MAYBE_GROW_GEOMETRICALLY

void
dlgraph_fork(void)
{
	static atomic_ulong global_graph_id = 0;

	assert(dl_this_worker);
	assert(!dl_this_worker->current_graph); /* TODO: No recursive graph */
	int nw = dl_this_worker->sched->nworkers;
	size_t wgsize = sizeof(struct dlgraph) + nw * sizeof(struct dlgraph_fragment);
	struct dlgraph *wg = malloc(wgsize);
	if (!wg) {
		perror("dlgraph_fork failed to allocate graph");
		exit(errno);
	}
	memset(wg, 0, wgsize);
	wg->id = atomic_fetch_add_explicit(&global_graph_id, 1,
	                                   memory_order_relaxed);
	wg->nworkers = nw;
	dl_this_worker->current_graph = wg;
}

void
dlgraph_join(const char *filename_prefix)
{
	assert(dl_this_worker);
	struct dlgraph *graph = dl_this_worker->current_graph;
	if (graph) {
		/* TODO: This is an ugly hack to include joining node */
		dlworker_add_current_node(dl_this_worker);
		if (filename_prefix)
			dlgraph_dump(graph, filename_prefix);
		dl_this_worker->current_graph = NULL;
		dlgraph_free(graph);
	}
}

void
dlgraph_label(const char *fmt, ...)
{
	assert(dl_this_worker);
	struct dlgraph_fragment *frag = dl_this_worker->current_graph->fragments +
	                                  dl_this_worker->index;
	va_list args;
	va_start(args, fmt);
	unsigned long length = 1 + vsnprintf(NULL, 0, fmt, args);
	dlgraph_fragment_maybegrow_label_buffer(frag, length);
	vsnprintf(frag->label_buffer + frag->label_buffer_count, length, fmt, args);
	dl_this_worker->current_node.label_offset = frag->label_buffer_count;
	frag->label_buffer_count += length;
	va_end(args);
}

unsigned long
dlgraph_link_node_description(struct dlgraph_node_description *desc)
{
	do {
		desc->next = atomic_load_explicit(&dl_node_description_lst_head,
		                                  memory_order_relaxed);
		desc->id = desc->next ? desc->next->id + 1 : 0;
	} while (!atomic_compare_exchange_strong_explicit(
	            &dl_node_description_lst_head,
	            &desc->next,
	            desc,
	            memory_order_seq_cst,
	            memory_order_relaxed));
	return desc->id;
}

void
dlgraph_add_continuation(struct dlgraph_fragment *frag, unsigned long head, unsigned long tail)
{
	dlgraph_fragment_maybegrow_continuations(frag, 1);
	frag->continuations[frag->continuations_count ++] = (struct dlgraph_edge) {
		/* unused: .ts_ns */
		.head = head,
		.tail = tail
	};
}

void
dlgraph_add_edge(struct dlgraph_fragment *frag, unsigned long head, unsigned long tail)
{
	dlgraph_fragment_maybegrow_edges(frag, 1);
	frag->edges[frag->edges_count ++] = (struct dlgraph_edge) {
		.ts_ns = dlgraph_now(),
		.head = head,
		.tail = tail
	};
}

void
dlgraph_add_node(struct dlgraph_fragment *frag, struct dlgraph_node *node)
{
	dlgraph_fragment_maybegrow_nodes(frag, 1);
	node->end_ns = dlgraph_now();
	frag->nodes[frag->nodes_count ++] = *node;
}

unsigned long long
dlgraph_now(void)
{
	struct timespec t;
	if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) {
		perror("dlgraph_now failed to call clock_gettime");
		exit(errno);
	}
	return (unsigned long long)t.tv_sec * 1000000000 + t.tv_nsec;
}

static void
dlgraph_dump(struct dlgraph *graph, const char *prefix)
{
	assert(dl_default_sched);

	size_t fnlen = 1+snprintf(NULL, 0, "%s%lu.dlg", prefix, graph->id);
	char *filename = calloc(fnlen, sizeof(*filename));
	if (!filename)
		goto panic;
	sprintf(filename, "%s%lu.dlg", prefix, graph->id);
	FILE *f = fopen(filename, "w");
	free(filename);
	if (!f)
		goto panic;

	struct dlgraph_node_description *head = dl_node_description_lst_head;
	fprintf(f, "%zu node descriptions\n", (size_t)(head->id + 1));
	write_node_descriptions_reverse(f, head);

	int nw = dl_default_sched->nworkers;
	size_t total_continuations = 0;
	size_t total_edges = 0;
	size_t total_nodes = 0;
	for (int w = 0; w < nw; ++ w) {
		struct dlgraph_fragment *frag = graph->fragments + w;
		total_continuations += frag->continuations_count;
		total_edges += frag->edges_count;
		total_nodes += frag->nodes_count;
	}

	fprintf(f, "%zu continuations\n", total_continuations);
	for (int w = 0; w < nw; ++ w) {
		struct dlgraph_fragment *frag = graph->fragments + w;
		for (size_t i = 0; i < frag->continuations_count; ++ i) {
			struct dlgraph_edge e = frag->continuations[i];
			fprintf(f, "%lu %lu\n", e.head, e.tail);
		}
	}

	fprintf(f, "%zu edges\n", total_edges);
	for (int w = 0; w < nw; ++ w) {
		struct dlgraph_fragment *frag = graph->fragments + w;
		for (size_t i = 0; i < frag->edges_count; ++ i) {
			struct dlgraph_edge e = frag->edges[i];
			fprintf(f, "%llu %lu %lu\n", e.ts_ns, e.head, e.tail);
		}
	}

	fprintf(f, "%zu nodes\n", total_nodes);
	for (int w = 0; w < nw; ++ w) {
		struct dlgraph_fragment *frag = graph->fragments + w;
		for (size_t i = 0; i < frag->nodes_count; ++ i) {
			struct dlgraph_node n = frag->nodes[i];
			const char *label = NULL;
			if (n.label_offset != ULONG_MAX) {
				label = frag->label_buffer + n.label_offset;
			}
			fprintf(f, "%s\n%d %lu %lu %llu %llu\n", label, w, n.task, n.desc, n.begin_ns, n.end_ns);
		}
	}

	if (ferror(f) || fclose(f) != 0)
		goto panic;

	return;
panic:
	perror("dlgraph_write failed to write to file");
	exit(errno);
}

static void
dlgraph_free(struct dlgraph *graph)
{
	size_t nw = graph->nworkers;
	for (size_t i = 0; i < nw; ++ i) {
		free(graph->fragments[i].label_buffer);
		free(graph->fragments[i].continuations);
		free(graph->fragments[i].edges);
		free(graph->fragments[i].nodes);
	}
	free(graph);
}

static void
write_node_descriptions_reverse(FILE *f, struct dlgraph_node_description *head)
{
	if (head) {
		write_node_descriptions_reverse(f, head->next);
		fprintf(f, "%s\n%lu\n%s\n", head->file, head->line, head->func);
	}
}

#endif /* DEADLOCK_GRAPH_EXPORT */


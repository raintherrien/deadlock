#ifndef DEADLOCK_INTERNAL_H_
#define DEADLOCK_INTERNAL_H_

#include <stdatomic.h>
#include <stddef.h>

#if !DEADLOCK_GRAPH_EXPORT

/*
 * struct dltask_ should be considered an opaque type by client code and only
 * manipulated by the public deadlock API.
 *
 * Architecturally, we have a simple structure with a function to invoke,
 * passing this structure as its only argument, a possibly NULL pointer to
 * some other task object which is currently awaiting this and possibly more
 * tasks to complete before executing, and a wait counter which counts how
 * many tasks this task is waiting on to execute. With this simple bottom-up
 * dependency chain, where one task can wait on many parent tasks, but a task
 * can only block a single child task, we can construct a DAG of tasks.
 *
 * When compiled with DEADLOCK_GRAPH_EXPORT struct dltask_ also stores a task
 * ID and a graph pointer, of which this task is a child.
 */
struct dltask_ {
	struct dltask_ *next_;
	dltaskfn fn_;
	atomic_uint wait_;
};

/*
 * When compiled without DEADLOCK_GRAPH_EXPORT DL_TASK_ENTRY() and
 * DL_TASK_INIT() are pretty basic.
 */
#define DL_TASK_ENTRY(outer_type, var, memb)                             \
	outer_type *var = DL_TASK_DOWNCAST(dlt_param, outer_type, memb); \
	(void) dlw_param;                                                \
	(void) var;

#define DL_TASK_INIT(fn) ((dltask){ .fn_ = fn })

#else /* DEADLOCK_GRAPH_EXPORT */

/*
 * Every graph node has a desc ID which refers to an entry in the linked list
 * dl_node_description_lst_head of static node description structures. This
 * structure defines compile-time attributes common to all nodes of this type.
 * This linked list is build statically by the macro DL_TASK_ENTRY().
 */
struct dlgraph_node_description {
	struct dlgraph_node_description *next;
	const char   *file;
	const char   *func;
	unsigned long id;
	unsigned long line;
};
extern _Atomic(struct dlgraph_node_description *) dl_node_description_lst_head;

/*
 * Nodes encode timing information, task and description IDs, and a
 * label_offset which is the offset of a runtime string describing this node
 * in the label_buffer of whatever graph_fragment owns this node, or ULONG_MAX
 * if this node has no label.
 */
struct dlgraph_node {
	unsigned long long begin_ns;
	unsigned long long end_ns;
	unsigned long task;
	unsigned long desc;
	unsigned long label_offset;
};

/*
 * An edge is defined by head and tail task IDs. *NOT* node IDs.
 */
struct dlgraph_edge {
	unsigned long head;
	unsigned long tail;
};

/*
 * A graph fragment is a portion of a complete graph populated by a single
 * thread.
 */
struct dlgraph_fragment {
	char *label_buffer;
	struct dlgraph_edge *edges;
	struct dlgraph_node *nodes;
	unsigned long label_buffer_count;
	unsigned long label_buffer_size;
	size_t edges_count;
	size_t edges_size;
	size_t nodes_count;
	size_t nodes_size;
};

/*
 * A graph is composed of fragments, one for each worker thread to populate
 * independently.
 */
struct dlgraph {
	int nworkers;
	struct dlgraph_fragment fragments[];
};

/*
 * See above for description of struct dltask_. This definition includes the
 * current graph pointer and task ID.
 */
struct dltask_ {
	struct dlgraph *graph_;
	struct dltask_ *next_;
	dltaskfn fn_;
	atomic_uint wait_;
	unsigned long tid_;
};

/* Worker methods to manipulate graph. Conditionally defined in worker.c */
void dlworker_set_current_node(void *worker, unsigned long description);
void dlworker_add_current_node(void *worker);
void dlworker_add_edge_from_current(void *worker, dltask *);

/*
 * When profiling we assign a unique ID to each task upon initialization. To
 * prevent false sharing each thread has its own non-atomic task ID generator.
 * Each threads' tasks are assigned a 24 bit unique ID and the most
 * significant byte identifies the thread.
 * TODO: Assert that we have less than 256 threads.
 */
extern _Thread_local unsigned long dl_next_task_id;
static inline unsigned long
dltask_next_id(void)
{
	unsigned long thread = dl_next_task_id & 0xFF000000;
	return dl_next_task_id = ((dl_next_task_id + 1) & 0x00FFFFFF) | thread;
}

/* Assignes the task a new ID and returns the old one. */
static inline unsigned long
dltask_xchg_id(dltask *t)
{
	unsigned long old = t->tid_;
	t->tid_ = dltask_next_id();
	return old;
}

/* Graph manipulation functions, conditionally defined in graph.c */
unsigned long dlgraph_link_node_description(struct dlgraph_node_description *);
void dlgraph_add_edge(struct dlgraph_fragment *, unsigned long h, unsigned long t);
void dlgraph_add_node(struct dlgraph_fragment *, struct dlgraph_node *);
unsigned long long dlgraph_now(void);

/*
 * When profiling each task function performs static initialization of a node
 * description superblock linked list entry, as well as initializing the
 * current node.
 */
#define DL_TASK_ENTRY(outer_type, var, memb)                             \
	do {                                                             \
		static struct dlgraph_node_description desc = {          \
			.file = __FILE__,                                \
			.func = __func__,                                \
			.line = __LINE__                                 \
		};                                                       \
		static atomic_int once = 1;                              \
		static unsigned long desc_id;                            \
		if (atomic_load_explicit(&once, memory_order_relaxed)) { \
			desc_id = dlgraph_link_node_description(&desc);  \
			atomic_store(&once, 0);                          \
		}                                                        \
		dlworker_set_current_node(dlw_param, desc_id);           \
	} while (0);                                                     \
	outer_type *var = DL_TASK_DOWNCAST(dlt_param, outer_type, memb); \
	(void) var;

/*
 * When profiling we must assign each task a unique ID
 */
#define DL_TASK_INIT(fn) ((dltask) { .fn_ = fn, .tid_ = dltask_next_id() })

#endif /* DEADLOCK_GRAPH_EXPORT */

#endif /* DEADLOCK_INTERNAL_H_ */

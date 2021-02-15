#ifndef DEADLOCK_GRAPH_H_
#define DEADLOCK_GRAPH_H_

#if DEADLOCK_GRAPH_EXPORT

/*
 * Deadlock exposes a simple graph visualization API.
 *
 * dlgraph_fork() may be invoked from within a task to create a new graph
 * which begins recording child tasks: "next" tasks and tasks invoked by
 * dlasync(). A graph must be joined by calling dlgraph_join().
 *
 * dlgraph_join() must be called to free the current graph. filename is an
 * optional file to write graph data into. If filename is NULL no file is
 * created.
 */
void dlgraph_fork(void);
void dlgraph_join(const char *filename);

/*
 * glgraph_label() sets the label of the current task, using printf like args.
 */
void dlgraph_label(const char *format, ...);

#else

static inline void dlgraph_fork(void) {}
static inline void dlgraph_join(const char *filename) { (void)filename; }
static inline void dlgraph_label(const char *format, ...) { (void) format; }

#endif

#endif /* DEADLOCK_GRAPH_H_ */

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
 * dlgraph_join() must be called to free the current graph. filename_prefix is
 * an optional file to write graph data into. If filename_prefix is NULL no
 * file is created. Otherwise, a file is created using filename_prefix as the
 * beginnings of a filename. TODO: This makes no sense
 */
void dlgraph_fork(void);
void dlgraph_join(const char *filename_prefix);

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

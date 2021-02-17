#include "deadlock/dl.h"
#include "deadlock/graph.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <Windows.h>
#else
#include <time.h> /* clock_gettime */
#endif

static struct A_pkg { dltask task; } A;
static struct B_pkg { dltask task; } B;
static struct C_pkg { dltask task; } C;
static struct D_pkg { dltask task; } D;
static struct E_pkg { dltask task; } E;
static struct F_pkg { dltask task; } F;
static DL_TASK_DECL(A_run);
static DL_TASK_DECL(B_run);
static DL_TASK_DECL(C_run);
static DL_TASK_DECL(D_run);
static DL_TASK_DECL(E_run);
static DL_TASK_DECL(F_run);

static void idle(unsigned int);

int
main(void)
{
	A = (struct A_pkg) { .task = DL_TASK_INIT(A_run) };
	B = (struct B_pkg) { .task = DL_TASK_INIT(B_run) };
	C = (struct C_pkg) { .task = DL_TASK_INIT(C_run) };
	D = (struct D_pkg) { .task = DL_TASK_INIT(D_run) };
	E = (struct E_pkg) { .task = DL_TASK_INIT(E_run) };
	F = (struct F_pkg) { .task = DL_TASK_INIT(F_run) };

	int result = dlmain(&A.task, NULL, NULL);
	if (result) perror("Error in dlmain");
	return result;
}

static DL_TASK_DECL(A_run)
{
	DL_TASK_ENTRY(struct A_pkg, aptr, task);

	dlgraph_fork();
	dlgraph_label("A task");

	dlnext(&B.task, &D.task);
	dlnext(&C.task, &D.task);
	dlwait(&D.task, 2);

	dlnext(&D.task, &F.task);
	dlnext(&E.task, &F.task);
	dlwait(&F.task, 2);

	dlasync(&B.task);
	dlasync(&C.task);

	idle(2);

	dlasync(&E.task);
}

static DL_TASK_DECL(B_run)
{
	DL_TASK_ENTRY(struct B_pkg, bptr, task);
	dlgraph_label("B task");
	idle(2);
}

static DL_TASK_DECL(C_run)
{
	DL_TASK_ENTRY(struct C_pkg, cptr, task);
	dlgraph_label("C task");
	idle(1);
}

static DL_TASK_DECL(D_run)
{
	DL_TASK_ENTRY(struct D_pkg, dptr, task);
	dlgraph_label("D task");
	idle(1);
}

static DL_TASK_DECL(E_run)
{
	DL_TASK_ENTRY(struct D_pkg, dptr, task);
	dlgraph_label("E task");
	idle(1);
}

static DL_TASK_DECL(F_run)
{
	DL_TASK_ENTRY(struct D_pkg, dptr, task);
	dlgraph_label("F task");
	idle(1);
	dlgraph_join("pretty-profile");
	dlterminate();
}

static void
idle(unsigned int ms)
{
#ifdef _WIN32
	Sleep(ms);
#else
	struct timespec timeout = { .tv_sec = 0, .tv_nsec = ms * 1000000 };
	(void) nanosleep(&timeout, NULL);
#endif
}

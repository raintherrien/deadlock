#include "deadlock/dl.h"
#include "deadlock/graph.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h> /* clock_gettime, nanosleep */
#endif

static struct A_pkg { dltask task; } A;
static struct B_pkg { dltask task; } B;
static struct C_pkg { dltask task; } C;
static struct D_pkg { dltask task; } D;
static struct E_pkg { dltask task; } E;
static struct F_pkg { dltask task; } F;
static void A_run(DL_TASK_ARGS);
static void B_run(DL_TASK_ARGS);
static void C_run(DL_TASK_ARGS);
static void D_run(DL_TASK_ARGS);
static void E_run(DL_TASK_ARGS);
static void F_run(DL_TASK_ARGS);

static void idle(unsigned int);

int
main(void)
{
	A = (struct A_pkg) { .task = dlcreate(A_run, NULL) };

	int result = dlmain(&A.task, NULL, NULL);
	if (result) perror("Error in dlmain");
	return result;
}

static void A_run(DL_TASK_ARGS)
{
	DL_TASK_ENTRY(struct A_pkg, aptr, task);

	F = (struct F_pkg) { .task = dlcreate(F_run, NULL) };
	E = (struct E_pkg) { .task = dlcreate(E_run, &F.task) };
	D = (struct D_pkg) { .task = dlcreate(D_run, &F.task) };
	C = (struct C_pkg) { .task = dlcreate(C_run, &D.task) };
	B = (struct B_pkg) { .task = dlcreate(B_run, &D.task) };

	dlgraph_fork();
	dlgraph_label("A task");

	idle(2);

	dldetach(&B.task);
	dldetach(&C.task);
	dldetach(&D.task);
	dldetach(&E.task);
	dldetach(&F.task);
}

static void B_run(DL_TASK_ARGS)
{
	DL_TASK_ENTRY(struct B_pkg, bptr, task);
	dlgraph_label("B task");
	idle(2);
}

static void C_run(DL_TASK_ARGS)
{
	DL_TASK_ENTRY(struct C_pkg, cptr, task);
	dlgraph_label("C task");
	idle(1);
}

static void D_run(DL_TASK_ARGS)
{
	DL_TASK_ENTRY(struct D_pkg, dptr, task);
	dlgraph_label("D task");
	idle(1);
}

static void E_run(DL_TASK_ARGS)
{
	DL_TASK_ENTRY(struct D_pkg, dptr, task);
	dlgraph_label("E task");
	idle(1);
}

static void F_run(DL_TASK_ARGS)
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

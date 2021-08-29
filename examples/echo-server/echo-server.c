/*
 * A very basic TCP echo server implementation using POSIX sockets.
 * - Listen port is by default 31337, because no one in their right mind
 * should run this priveledged, but this can be set upon execution.
 * - Only a TCP echo server
 *
 * Test with e.g.: netcat 127.0.0.1 31337
 */

#include "deadlock/dl.h"
#include "deadlock/graph.h"

#include <assert.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

/* start_listen finds and binds us a free socket */
int start_listen(const char *port);

struct accept {
	dltask task;
	dltask exit;
	int socketfd;
};

struct connection {
	dltask task;
	int clientfd;
};
static void entry_run(DL_TASK_ARGS);
static void accept_run(DL_TASK_ARGS);
static void echo_run(DL_TASK_ARGS);
static void exit_run(DL_TASK_ARGS);

static atomic_int should_close = 0;

int
main(int argc, char** argv)
{
	if (argc > 1 && argv[1] && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
		puts("Usage: echo-server [LISTEN-PORT]\n");
		return EXIT_SUCCESS;
	}

	const char *portstr = argc == 2 ? argv[1] : "31337";
	int socketfd = start_listen(portstr);

	printf("Listening on port %s, send '!' to exit\n", portstr);

	/*
	 * Transfer complete control to a Deadlock scheduler. This
	 * function will return when our scheduler is terminated.
	 */
	struct accept accept = {
		.task = DL_TASK_INIT(entry_run),
		.socketfd = socketfd
	};
	int result = dlmain(&accept.task, NULL, NULL);
	if (result) perror("Error in dlmain");

	close(socketfd);

	return 0;
}

int
start_listen(const char *port)
{
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family   = AF_INET;     /* ipv4 */
	hints.ai_socktype = SOCK_STREAM; /* tcp */
	hints.ai_flags    = AI_PASSIVE;  /* bind & accept socket */

	struct addrinfo *result;
	int rc = getaddrinfo(NULL, port, &hints, &result);
	switch (rc) {
	case 0: break;
	case EAI_SYSTEM:
		fprintf(stderr, "getaddrinfo failed with port %s: %s",
		        port, strerror(errno));
		exit(EXIT_FAILURE);
	default:
		fprintf(stderr, "getaddrinfo failed with port %s: %s",
		        port, gai_strerror(rc));
		exit(EXIT_FAILURE);
	}

	int sfd;
	for (struct addrinfo *rp = result; rp; rp = rp->ai_next) {
		sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (sfd == -1) continue;
		if (bind(sfd, rp->ai_addr, rp->ai_addrlen) == 0) break;
		close(sfd);
		sfd = -1;
	}

	freeaddrinfo(result);

	if (sfd == -1) {
		fprintf(stderr, "Could not bind to port %s\n", port);
		exit(EXIT_FAILURE);
	}

	if (listen(sfd, 1024)) {
		perror("listen on socket with backlog 1024 failed");
		exit(EXIT_FAILURE);
	}

	printf("Server started at port %s\n", port);
	return sfd;
}

static void
entry_run(DL_TASK_ARGS)
{
	DL_TASK_ENTRY(struct accept, pkg, task);
	dlgraph_fork();
	dltail(&pkg->task, accept_run);

	/* Make sure accept is complete before calling exit */
	pkg->exit = DL_TASK_INIT(exit_run);
	dlnext(&pkg->task, &pkg->exit);
	dlwait(&pkg->exit, 1);
}

static void
exit_run(DL_TASK_ARGS)
{
	DL_TASK_ENTRY(struct accept, pkg, task);
	dlgraph_join("echo");
	dlterminate();
}

static void
accept_run(DL_TASK_ARGS)
{
	DL_TASK_ENTRY(struct accept, pkg, task);

	struct sockaddr_in addr;
	socklen_t          addrlen = sizeof(addr);

	int wait = 0;
	while (wait == 0) {
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(pkg->socketfd, &fds);
		struct timeval timeout = { .tv_sec = 2, .tv_usec = 0 };

		wait = select(pkg->socketfd+1, &fds, NULL, NULL, &timeout);
		if (wait == -1) {
			perror("select socket failed");
			return;
		}

		/* No cleanup */
		if (should_close) {
			return;
		}
	}

	int clientfd = accept(pkg->socketfd, (struct sockaddr *)&addr, &addrlen);
	if (clientfd == -1) {
		perror("accept client failed");
		return;
	}

	struct connection *rw = malloc(sizeof *rw);
	if (!rw) {
		perror("Failed to allocate connection");
		return;
	}
	*rw = (struct connection) {
		.task = DL_TASK_INIT(echo_run),
		.clientfd = clientfd
	};

	/* Make sure rw is complete before calling exit */
	dlwait(&pkg->exit, 1);
	dlnext(&rw->task, &pkg->exit);
	dlasync(&rw->task);

	/* Recursive! With no termination :) have fun! */
	dltail(&pkg->task, accept_run);
}

static void
echo_run(DL_TASK_ARGS)
{
	DL_TASK_ENTRY(struct connection, pkg, task);

	char buf[4096];

	int wait = 0;
	while (wait == 0) {
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(pkg->clientfd, &fds);
		struct timeval timeout= { .tv_sec = 2, .tv_usec = 0 };

		wait = select(pkg->clientfd+1, &fds, NULL, NULL, &timeout);
		if (wait == -1) {
			perror("select socket failed");
			goto close_conn;
		}

		if (should_close) {
			goto close_conn;
		}
	}

	ssize_t len = recv(pkg->clientfd, buf, 4096, 0);

	if (len ==  0) goto close_conn; /* non-error */
	if (len == -1) {
		perror("Failed to recieve from client");
		goto close_conn;
	}

	for (size_t i = 0; i < (size_t)len; ++ i) {
		if (buf[i] == '!') {
			puts("Received '!', closing connection\n");
			should_close = 1;
			break;
		}
	}

	char *wbuf = buf;
	while (len > 0) {
		ssize_t written = write(pkg->clientfd, wbuf, len);
		if (written ==  0) goto close_conn; /* non-error */
		if (written == -1) {
			perror("Failed to write to client");
			goto close_conn;
		}
		assert(written <= len);
		len -= written;
		wbuf += written;
	}

	if (!should_close) {
		dltail(&pkg->task, echo_run);
		return;
	}

close_conn:
	(void) shutdown(pkg->clientfd, SHUT_RDWR);
	(void) close(pkg->clientfd);
	free(pkg);
}

#include "deadlock/dl.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

/* start_listen finds and binds us a free socket */
int start_listen(const char *port);

/*
 * accept blocks for a client connection then spawns connection with the
 * client file descriptor and recursively schedules itself on success.
 */
struct accept_pkg {
	dltask task;
	int socketfd;
};
static void accept_run(DL_TASK_ARGS);

/*
 * read reads payload from client, performs zero validation, and queues
 * a write to showcast dlcontinuation().
 *
 * Out of sheer laziness, define a generic task for both read and write.
 */
struct rw_pkg {
	dltask task;
	int clientfd;
};
static void read_run(DL_TASK_ARGS);
static void write_run(DL_TASK_ARGS);

int
main(int argc, char** argv)
{
	int socketfd = start_listen(argc == 2 ? argv[1] : "31337");

	/*
	 * Transfer complete control to a Deadlock scheduler. This
	 * function will return when our scheduler is terminated.
	 */
	struct accept_pkg accept = {
		.task = DL_TASK_INIT(accept_run),
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
accept_run(DL_TASK_ARGS)
{
	DL_TASK_ENTRY(struct accept_pkg, pkg, task);

	struct sockaddr_in addr;
	socklen_t          addrlen = sizeof(addr);
	int clientfd = accept(pkg->socketfd, (struct sockaddr *)&addr, &addrlen);
	if (clientfd == -1) {
		perror("accept client failed");
		goto error;
	}

	struct rw_pkg *rw = malloc(sizeof *rw);
	if (!rw) {
		perror("Failed to allocate rw_pkg");
		goto error;
	}
	*rw = (struct rw_pkg) {
		.task = DL_TASK_INIT(read_run),
		.clientfd = clientfd
	};

	dlasync(&rw->task);
	/* Recursive! With no termination :) have fun! */
	dltail(&pkg->task, accept_run);

	return;

error:
	dlterminate();
}

static void
read_run(DL_TASK_ARGS)
{
	DL_TASK_ENTRY(struct rw_pkg, pkg, task);

	char msg[4096];

	ssize_t len = recv(pkg->clientfd, msg, 4096, 0);

	if (len ==  0) goto close_conn; /* non-error */
	if (len == -1) {
		perror("Failed to recieve from client");
		goto close_conn;
	}
	if (len == 4096) {
		fprintf(stderr, "Message from client too long for buffer\n");
		goto close_conn;
	}

	msg[len] = '\0';
	printf("Received from client on worker %d:\n"
	       "\033[32m%s\033[m\n",
	       dlworker_index(), msg);

	/*
	 * Totally ignore what the client has to say and return some HTML.
	 */
	dlcontinuation(&pkg->task, write_run);
	dlasync(&pkg->task);
	return;

close_conn:
	(void) shutdown(pkg->clientfd, SHUT_RDWR);
	(void) close(pkg->clientfd);
	free(pkg);
}

static void
write_run(DL_TASK_ARGS)
{
	DL_TASK_ENTRY(struct rw_pkg, pkg, task);

	char response[64];
	ssize_t len = snprintf(response, 64,
	                       "HTTP/1.0 200 OK\n\n"
	                       "<html>You've been served by worker %d",
	                       dlworker_index());
	if (len < 0 || len >= 64) {
		perror("Failed to construct response");
		goto close_conn;
	}
	ssize_t written = write(pkg->clientfd, response, len);
	if (written != len) {
		perror("Failed to write to client");
	}

close_conn:
	(void) shutdown(pkg->clientfd, SHUT_RDWR);
	(void) close(pkg->clientfd);
	free(pkg);
}


// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021 Felix Fietkau <nbd@nbd.name> */
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

#include <libubox/usock.h>

#include "rcd.h"

static LIST_HEAD(servers);
static LIST_HEAD(pending);
static bool in_init;
static struct uloop_timeout restart_timer;

static void
server_cb(struct uloop_fd *fd, unsigned int events)
{
	struct server *s = container_of(fd, struct server, fd);
	struct sockaddr_in6 addr;
	unsigned int sl;
	int cfd;

	while (1) {
		sl = sizeof(addr);
		cfd = accept(fd->fd, (struct sockaddr *)&addr, &sl);

		if (cfd < 0) {
			if (errno == EAGAIN)
				return;

			if (errno == EINTR)
				continue;

			/* other error, restart */
			uloop_fd_delete(fd);
			close(fd->fd);
			list_move_tail(&s->list, &pending);
			uloop_timeout_set(&restart_timer, 1000);
		}

		rcd_client_accept(cfd);
	}
}

#ifdef CONFIG_ZSTD
static void
zstd_server_cb(struct uloop_fd *fd, unsigned int events)
{
	struct server *s = container_of(fd, struct server, zfd);
	struct sockaddr_in6 addr;
	unsigned int sl;
	int cfd;

	while (1) {
		sl = sizeof(addr);
		cfd = accept(fd->fd, (struct sockaddr *)&addr, &sl);

		if (cfd < 0) {
			if (errno == EAGAIN)
				return;

			if (errno == EINTR)
				continue;

			/* other error, restart */
			uloop_fd_delete(fd);
			close(fd->fd);
			list_move_tail(&s->list, &pending);
			uloop_timeout_set(&restart_timer, 1000);
		}

		rcd_zclient_accept(cfd);
	}
}
#endif

static int
server_fd_init(struct uloop_fd *fd, const char *addr, int port, uloop_fd_handler cb)
{
	fd->fd = usock(USOCK_SERVER | USOCK_NONBLOCK | USOCK_TCP, addr, usock_port(port));
	if (fd->fd < 0) {
		if (in_init)
			fprintf(stderr, "WARNING: Failed to open server port %d on %s\n", port, addr);
		return -1;
	}

	fd->cb = cb;
	uloop_fd_add(fd, ULOOP_READ);

	return 0;
}

static void server_start(struct server *s)
{
	int err = 0;

	err += server_fd_init(&s->fd, s->addr, RCD_PORT, server_cb);
#ifdef CONFIG_ZSTD
	err += server_fd_init(&s->zfd, s->addr, RCD_PORT + 1, zstd_server_cb);
#endif
	if (err)
		return;

	list_move_tail(&s->list, &servers);
}

static void
server_start_pending(struct uloop_timeout *timeout)
{
	struct server *s, *tmp;

	list_for_each_entry_safe(s, tmp, &pending, list)
		server_start(s);

	if (!list_empty(&pending))
		uloop_timeout_set(timeout, 1000);
}

void rcd_server_add(const char *addr)
{
	struct server *s;

	s = calloc(1, sizeof(*s));
	s->addr = addr;
	list_add_tail(&s->list, &pending);

}

void rcd_server_init(void)
{
	if (list_empty(&pending))
		rcd_server_add("127.0.0.1");

	restart_timer.cb = server_start_pending;

	in_init = true;
	server_start_pending(&restart_timer);
	in_init = false;
}

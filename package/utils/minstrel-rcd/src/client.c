// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021 Felix Fietkau <nbd@nbd.name> */
#include <libgen.h>
#include "rcd.h"

static LIST_HEAD(clients);
#ifdef CONFIG_ZSTD
static LIST_HEAD(zclients);
#endif

static const char *
phy_driver(struct phy *phy)
{
	static char driver[16];
	char path[64], buf[64];
	size_t len;
	ssize_t n_written;

	len = snprintf(path, sizeof(path), "/sys/class/ieee80211/%s/device/driver", phy_name(phy));
	if (len >= sizeof(path))
		return NULL;

	n_written = readlink(path, buf, sizeof(buf));
	if (n_written == sizeof(buf))
		return NULL;

	buf[n_written] = '\0';
	strncpy(driver, basename(buf), sizeof(driver));

	return driver;
}

void rcd_client_phy_event(struct phy *phy, const char *str)
{
	struct client *cl;

	list_for_each_entry(cl, &clients, list)
		client_phy_printf(cl, phy, "%s\n", str);
}

void rcd_client_set_phy_state(struct client *cl, struct phy *phy, bool add)
{
	if (!cl) {
		list_for_each_entry(cl, &clients, list)
			rcd_client_set_phy_state(cl, phy, add);
		return;
	}

	if (add && !cl->init_done) {
		rcd_phy_dump(cl, phy);
		cl->init_done = true;
	}

	client_phy_printf(cl, phy, "0;%s;%s\n", add ? "add" : "remove", phy_driver(phy));
}

static void
client_start(struct client *cl)
{
	struct phy *phy;

	vlist_for_each_element(&phy_list, phy, node)
		rcd_client_set_phy_state(cl, phy, true);
}

static int
client_handle_data(struct client *cl, char *data)
{
	char *sep;
	int len = 0;

	while ((sep = strchr(data, '\n')) != NULL) {
		len += sep - data + 1;
		if (sep[-1] == '\r')
			sep[-1] = 0;
		*sep = 0;
		rcd_phy_control(cl, data);
		data = sep + 1;
	}

	return len;
}

static void
client_notify_read(struct ustream *s, int bytes)
{
	struct client *cl = container_of(s, struct client, sfd.stream);
	char *data;
	int len;

	while (1) {
		data = ustream_get_read_buf(s, &len);
		if (!data)
			return;

		len = client_handle_data(cl, data);
		if (!len)
			return;

		ustream_consume(s, len);
	}
}

static void
client_notify_state(struct ustream *s)
{
	struct client *cl = container_of(s, struct client, sfd.stream);

	if (!s->write_error && !s->eof)
		return;

	ustream_free(s);
	close(cl->sfd.fd.fd);
	list_del(&cl->list);
	free(cl);
}

void rcd_client_accept(int fd)
{
	struct ustream *us;
	struct client *cl;

	cl = calloc(1, sizeof(*cl));
	us = &cl->sfd.stream;
	us->notify_read = client_notify_read;
	us->notify_state = client_notify_state;
	us->string_data = true;
	ustream_fd_init(&cl->sfd, fd);
	list_add_tail(&cl->list, &clients);
	client_start(cl);
}


#ifdef CONFIG_ZSTD
bool
rcd_has_clients(bool compressed)
{
	return compressed ? !list_empty(&zclients) : !list_empty(&clients);
}

static void
zclient_start(struct client *cl)
{
	struct phy *phy;

	vlist_for_each_element(&phy_list, phy, node)
		rcd_zclient_set_phy_state(cl, phy, true);
}

void rcd_zclient_accept(int fd)
{
	struct ustream *us;
	struct client *cl;

	cl = calloc(1, sizeof(*cl));
	us = &cl->sfd.stream;
	us->notify_read = client_notify_read;
	us->notify_state = client_notify_state;
	us->string_data = true;
	ustream_fd_init(&cl->sfd, fd);
	list_add_tail(&cl->list, &zclients);
	zclient_start(cl);
}

void rcd_zclient_set_phy_state(struct client *cl, struct phy *phy, bool add)
{
	char str[16];
	char buf[128]; // worst-case compress bound for 16 bytes is 79
	size_t clen;
	int error;

	if (!cl) {
		list_for_each_entry(cl, &zclients, list)
			rcd_zclient_set_phy_state(cl, phy, add);
		return;
	}

	if (add && !cl->init_done) {
		rcd_phy_dump_zstd(cl, phy);
		cl->init_done = true;
	}

	snprintf(str, sizeof(str), "%s;0;%s\n", phy_name(phy), add ? "add" : "remove");

	error = zstd_compress_into(&buf, sizeof(buf), str, strlen(str), &clen);
	if (error)
		return;

	client_write(cl, buf, clen);
}

void rcd_zclient_write(const void *buf, size_t len)
{
	struct client *cl;

	list_for_each_entry(cl, &zclients, list)
		client_write(cl, buf, len);
}
#endif

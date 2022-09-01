// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021 Felix Fietkau <nbd@nbd.name> */
#include <libubox/avl-cmp.h>
#include <glob.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <libgen.h>
#include "rcd.h"

static void phy_update(struct vlist_tree *tree, struct vlist_node *node_new,
		       struct vlist_node *node_old);

VLIST_TREE(phy_list, avl_strcmp, phy_update, true, false);

static const char *
phy_file_path(struct phy *phy, const char *file)
{
	static char path[64];

	snprintf(path, sizeof(path), "/sys/kernel/debug/ieee80211/%s/rc/%s", phy_name(phy), file);

	return path;
}

static const char *
phy_debugfs_file_path(struct phy *phy, const char *file)
{
	static char path[64];

	snprintf(path, sizeof(path), "/sys/kernel/debug/ieee80211/%s/%s",
	         phy_name(phy), file);

	return path;
}

static int
phy_event_read_buf(struct phy *phy, char *buf)
{
	char *cur, *next;
	int len;

	for (cur = buf; (next = strchr(cur, '\n')); cur = next + 1) {
		*next = 0;

		rcd_client_phy_event(phy, cur);
#ifdef CONFIG_ZSTD
		zstd_read_fmt("%s;%s\n", phy_name(phy), cur);
#endif
#ifdef CONFIG_MQTT
		mqtt_phy_event(phy, cur);
#endif
	}

	len = strlen(cur);
	if (cur > buf)
		memmove(buf, cur, len + 1);

	return len;
}

static void
phy_event_cb(struct uloop_fd *fd, unsigned int events)
{
	struct phy *phy = container_of(fd, struct phy, event_fd);
	char buf[512];
	int len, offset = 0;

	while (1) {
		len = read(fd->fd, buf + offset, sizeof(buf) - 1 - offset);
		if (len < 0) {
			if (errno == EAGAIN)
				return;

			if (errno == EINTR)
				continue;

			vlist_delete(&phy_list, &phy->node);
			return;
		}

		if (!len)
			return;

		buf[offset + len] = 0;
		offset = phy_event_read_buf(phy, buf);
	}
}

static void
phy_init(struct phy *phy)
{
	phy->control_fd = -1;
}

static void
phy_add(struct phy *phy)
{
	int cfd, efd;

	cfd = open(phy_file_path(phy, "api_control"), O_WRONLY);
	if (cfd < 0)
		goto remove;

	efd = open(phy_file_path(phy, "api_event"), O_RDONLY);
	if (efd < 0)
		goto close_cfd;

	phy->control_fd = cfd;
	phy->event_fd.fd = efd;
	phy->event_fd.cb = phy_event_cb;
	uloop_fd_add(&phy->event_fd, ULOOP_READ);

	rcd_client_set_phy_state(NULL, phy, true);
	return;

close_cfd:
	close(cfd);
remove:
	vlist_delete(&phy_list, &phy->node);
}

static void
phy_remove(struct phy *phy)
{
	if (phy->control_fd < 0)
		goto out;

	rcd_client_set_phy_state(NULL, phy, false);
	uloop_fd_delete(&phy->event_fd);
	close(phy->control_fd);
	close(phy->event_fd.fd);

out:
	free(phy);
}

static void
phy_update(struct vlist_tree *tree, struct vlist_node *node_new,
	   struct vlist_node *node_old)
{
	struct phy *phy_new = node_new ? container_of(node_new, struct phy, node) : NULL;
	struct phy *phy_old = node_old ? container_of(node_old, struct phy, node) : NULL;

	if (phy_new && phy_old)
		phy_remove(phy_new);
	else if (phy_new)
		phy_add(phy_new);
	else
		phy_remove(phy_old);
}

static void phy_refresh_timer(struct uloop_timeout *t)
{
	unsigned int i;
	glob_t gl;

	glob("/sys/kernel/debug/ieee80211/phy*", 0, NULL, &gl);
	for (i = 0; i < gl.gl_pathc; i++) {
		struct phy *phy;
		char *name, *name_buf;

		name = basename(gl.gl_pathv[i]);
		phy = calloc_a(sizeof(*phy), &name_buf, strlen(name) + 1);
		phy_init(phy);
		vlist_add(&phy_list, &phy->node, strcpy(name_buf, name));
	}
	globfree(&gl);

	uloop_timeout_set(t, 1000);
}

void rcd_phy_init_client(struct client *cl)
{
	struct phy *phy;

	vlist_for_each_element(&phy_list, phy, node)
		rcd_client_set_phy_state(cl, phy, true);
}

#ifdef CONFIG_ZSTD
void rcd_phy_dump_zstd(struct client *cl, struct phy *phy)
{
	char buf[512];
	void *compressed;
	size_t clen;
	FILE *f;
	int error;

	f = fopen(phy_file_path(phy, "api_info"), "r");
	if (!f)
		return;

	while (fgets(buf, sizeof(buf), f) != NULL) {
		error = zstd_fmt_compress(&compressed, &clen, "*;0;%s", buf);
		if (error) {
			fclose(f);
			return;
		}

		client_write(cl, compressed, clen);
		free(compressed);
	}

	fclose(f);
}
#endif

void rcd_phy_dump(struct client *cl, struct phy *phy)
{
	char buf[512];
	FILE *f;

	f = fopen(phy_file_path(phy, "api_info"), "r");
	if (!f)
		return;

	while (fgets(buf, sizeof(buf), f) != NULL)
		client_printf(cl, "*;0;%s", buf);

	fclose(f);
}

#ifdef CONFIG_MQTT
void mqtt_phy_dump(struct phy *phy, int (*cb)(void*, char*), void *cb_arg)
{
	char buf[512];
	FILE *f;

	f = fopen(phy_file_path(phy, "api_info"), "r");
	if (!f)
		return;

	while (fgets(buf, sizeof(buf), f) != NULL)
		cb(cb_arg, buf);

	fclose(f);
}
#endif

static int
phy_fd_write(int fd, const char *s)
{
retry:
	if (write(fd, s, strlen(s)) < 0) {
		if (errno == EINTR || errno == EAGAIN)
			goto retry;

		return errno;
	}

	return 0;
}

static int
phy_debugfs_read(struct client *cl, struct phy *phy, const char *file, bool compressed)
{
	char *buf, *cur;
	int fd, len, err = 0, offset = 0, bufsiz = 512;
#ifdef CONFIG_ZSTD
	void *cbuf;
	size_t clen;
#endif

	fd = open(phy_debugfs_file_path(phy, file), O_RDONLY);
	if (fd < 0)
		return errno;

	buf = malloc(bufsiz);

	while (1) {
		if (!buf) {
			err = -ENOMEM;
			goto error;
		}

		while (1) {
			len = read(fd, buf + offset, bufsiz - 1 - offset);
			if (len == 0) {
				if (buf[offset + len - 1] == '\n')
					buf[offset + len - 1] = '\0';
				else
					buf[offset + len] = '\0';
				goto done;
			} else if (len < 0) {
				if (errno == EAGAIN) {
					err = errno;
					goto error;
				}

				if (errno == EINTR)
					continue;

				free(buf);
				goto error;
			} else if (len == bufsiz - 1 - offset) {
				bufsiz *= 2;
				buf = realloc(buf, bufsiz);
				break;
			}

			offset += len;
		}
	}
done:
	for (cur = buf; *cur != '\0'; cur++)
		if (*cur == '\n')
			*cur = ',';

#ifdef CONFIG_ZSTD
	if (compressed) {
		err = zstd_fmt_compress(&cbuf, &clen, "%s;0;debugfs;%s;%s\n",
		                        phy_name(phy), file, buf);
		if (err) {
			free(buf);
			goto error;
		}

		client_write(cl, cbuf, clen);
		free(cbuf);
	} else
#endif
	{
		client_phy_printf(cl, phy, "0;debugfs;%s;%s\n", file, buf);
	}

	free(buf);
error:
	close(fd);
	return err;
}

static int
phy_debugfs_write(struct phy *phy, const char *file, const char *arg)
{
	int err, fd;

	fd = open(phy_debugfs_file_path(phy, file), O_WRONLY);
	if (fd < 0)
		return errno;

	err = phy_fd_write(fd, arg);
	close(fd);

	return err;
}

static int
phy_debugfs(struct client *cl, struct phy *phy, char *cmd, bool compressed)
{
	char *file, *arg;

	file = strtok(cmd, ";");
	arg = strtok(NULL, ";");

	/* make sure file cannot contain dots as this would
	 * allow writing to files with root privileges if
	 * file is something like '../../../../ ...
	 * Also, limit path length to 64 chars as a precaution.
	 * */
	if (!file || strchr(file, '.') || strlen(file) > 64)
		return -EINVAL;

	if (arg)
		return phy_debugfs_write(phy, file, arg);
	else
		return phy_debugfs_read(cl, phy, file, compressed);
}

void rcd_phy_control(struct client *cl, char *data, bool compressed)
{
	struct phy *phy;
	const char *err = "Syntax error";
	char *sep;
#ifdef CONFIG_ZSTD
	void *buf;
	size_t clen;
#endif
	int error;

	sep = strchr(data, ';');
	if (!sep)
		goto error;

	*sep = 0;
	phy = vlist_find(&phy_list, data, phy, node);
	if (!phy) {
		err = "PHY not found";
		goto error;
	}

	data = sep + 1;

	sep = strchr(data, ';');
	if (sep) {
		*sep = 0;
		if (strcmp(data, "debugfs") == 0) {
			error = phy_debugfs(cl, phy, sep + 1, compressed);
			if (error) {
				err = strerror(error);
				goto error;
			} else {
				return;
			}
		}

		*sep = ';';
	}

	error = phy_fd_write(phy->control_fd, data);
	if (error) {
		err = strerror(error);
		goto error;
	}

	return;

error:
#ifdef CONFIG_ZSTD
	if (compressed) {
		error = zstd_fmt_compress(&buf, &clen, "*;0;#error;%s\n", err);
		if (error)
			return;
		client_write(cl, buf, clen);
		free(buf);
	} else
#endif
	{
		client_printf(cl, "*;0;#error;%s\n", err);
	}
}

void rcd_phy_init(void)
{
	static struct uloop_timeout t = {
		.cb = phy_refresh_timer
	};

	uloop_timeout_set(&t, 1);
}

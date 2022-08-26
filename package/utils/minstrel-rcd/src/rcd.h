// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021 Felix Fietkau <nbd@nbd.name> */
#ifndef __MINSTREL_RCD_H
#define __MINSTREL_RCD_H

#include <libubox/list.h>
#include <libubox/vlist.h>
#include <libubox/uloop.h>
#include <libubox/ustream.h>
#include <libubox/utils.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#ifdef CONFIG_MQTT
#include <mosquitto.h>

#define MQTT_PORT 1883
#endif

#define RCD_PORT 0x5243

extern const char *config_path;

#ifdef CONFIG_MQTT
extern const char *global_id;
extern const char *global_topic;
#endif

struct phy {
	struct vlist_node node;

	struct uloop_fd event_fd;
	int control_fd;
};

struct client {
	struct list_head list;
	struct ustream_fd sfd;
	bool init_done;
};

struct server {
	struct list_head list;
	struct uloop_fd fd;
#ifdef CONFIG_ZSTD
	struct uloop_fd zfd;
#endif
	const char *addr;
};

#ifdef CONFIG_MQTT
struct mqtt_context {
	struct list_head list;
	struct mosquitto *mosq;
	const char *id;
	char *addr;
	int port;
	const char *bind_addr;
	const char *topic_prefix;
	struct uloop_fd fd;
	bool init_done;
};
#endif


static inline const char *phy_name(struct phy *phy)
{
	return phy->node.avl.key;
}

extern struct vlist_tree phy_list;

void rcd_server_add(const char *addr);
void rcd_server_init(void);

void rcd_client_accept(int fd);
void rcd_client_phy_event(struct phy *phy, const char *str);
void rcd_client_set_phy_state(struct client *cl, struct phy *phy, bool add);

void rcd_phy_init(void);
void rcd_phy_init_client(struct client *cl);
void rcd_phy_dump(struct client *cl, struct phy *phy);
void rcd_phy_control(struct client *cl, char *data, bool compression);

#define client_printf(cl, ...) ustream_printf(&(cl)->sfd.stream, __VA_ARGS__)
#define client_vprintf(cl, va_args) ustream_vprintf(&(cl)->sfd.stream, va_args)
#define client_phy_printf(cl, phy, fmt, ...) client_printf(cl, "%s;" fmt, phy_name(phy), ## __VA_ARGS__)

void rcd_config_init(void);

#ifdef CONFIG_MQTT
int mqtt_config_init(void);
void mqtt_init(void);
void mqtt_broker_add(const char *addr, int port, const char *bind, const char *id,
                     const char *prefix, const char *capath);
void mqtt_broker_add_cli(const char *addr, const char *bind, const char *id, const char *prefix,
                         const char *capath);
int mqtt_publish_event(const struct phy *phy, const char *str);
void mqtt_stop(void);

void mqtt_phy_dump(struct phy *phy, int (*cb)(void *, char*), void *cb_arg);
void mqtt_phy_event(struct phy *phy, const char *str);
#endif

#ifdef CONFIG_ZSTD
#define client_write(cl, buf, len) ustream_write(&(cl)->sfd.stream, buf, len, false)

bool rcd_has_clients(bool compressed);

struct zstd_opts {
	const char *dict;
	int comp_level;
	size_t bufsize;
	int timeout_ms;
};

#define ZSTD_OPTS_DEFAULTS {\
	.dict = "/lib/minstrel-rcd/dictionary.zdict",\
	.comp_level = 3,\
	.bufsize = 4096,\
	.timeout_ms = 1000,\
}

void config_init_zstd(struct zstd_opts *o);

int zstd_init(const struct zstd_opts *o);
int zstd_compress(void *data, size_t len, void **compressed, size_t *clen);
int zstd_compress_into(void *dst, size_t dstlen, void *data, size_t len, size_t *complen);
int zstd_fmt_compress(void **compressed, size_t *clen, const char *fmt, ...);
void zstd_stop(bool flush);
int zstd_read_fmt(const char *fmt, ...);

void rcd_zclient_accept(int fd);
void rcd_zclient_set_phy_state(struct client *cl, struct phy *phy, bool add);
void rcd_zclient_write(const void *buf, size_t len);

void rcd_phy_dump_zstd(struct client *cl, struct phy *phy);
#endif

#endif

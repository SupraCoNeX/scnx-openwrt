#include <errno.h>
#include <mqtt_protocol.h>
#include "rcd.h"

static LIST_HEAD(brokers);
static LIST_HEAD(pending);
static struct uloop_timeout restart_timer;
static struct uloop_timeout mosquitto_misc_timer;

#define KEEPALIVE_SECONDS 5 // TODO: make configurable
#define MOSQUITTO_MAINTENANCE_FREQ_MS 1000

/* Max length for topic string is 64KB
 *(https://docs.oasis-open.org/mqtt/mqtt/v5.0/os/mqtt-v5.0-os.html#_UTF-8_Encoded_String)
 */
#define TOPIC_MAXLEN 65536
#define TOPIC_BASELEN 5 // '.../api/...'

static int
validate_topic(const char *topic, const char *id)
{
	size_t topic_len;

	if (topic) {
		topic_len = strlen(topic);

		if (topic[topic_len - 1] != '/') {
			fprintf(stderr, "WARNING: Topic prefix '%s' does not end with '/'\n", topic);
			return -1;
		}
	} else {
		topic_len = 0;
	}

	if (!id) {
		fprintf(stderr, "WARNING: Skipping a broker because no ID was given.\n");
		return -1;
	}

	if (topic_len + strlen(id) + TOPIC_BASELEN > TOPIC_MAXLEN) {
		fprintf(stderr, "Given topic strings can exceed the maximum length of 64KB. "
			"Please reconsider what you are doing.\n");
		return -1;
	}

	return 0;
}

static int
__add_broker(char *addr, int port, const char *bind_addr, const char *id, const char *topic)
{
	struct mqtt_context *ctx;

	ctx = calloc(1, sizeof(*ctx));
	if (!ctx) {
		fprintf(stderr, "ERROR: Out of memory!\n");
		return -1;
	}

	ctx->id = id;
	ctx->addr = addr;
	ctx->port = port;
	ctx->bind_addr = bind_addr ? bind_addr : "::";
	ctx->topic_prefix = topic ? topic : "";

	printf("add broker {'addr': %s, 'port': '%d', 'bind': %s, 'id': '%s', 'prefix': '%s'}\n",
		ctx->addr, ctx->port, ctx->bind_addr, ctx->id, ctx->topic_prefix);

	list_add_tail(&ctx->list, &pending);
	return 0;
}

void
mqtt_broker_add_cli(const char *addr, const char *bind_addr, const char *id,
					const char *topic_prefix)
{
	char *sep, *buf;
	int port, err;

	err = validate_topic(topic_prefix, id);
	if (err)
		return;

	/* IPv6 addresses are enclosed in [] */
	if (*addr == '[') {
		sep = strrchr(addr, ']');
		if (!sep) {
			fprintf(stderr, "ERROR: invalid IPv6 address '%s' (enclose in '[]')\n", addr);
			return;
		} else {
			++sep;
		}
	} else {
		sep = strchrnul(addr, ':');
	}

	port = (sep[0]) ? (sep[1] ? atoi(sep + 1) : MQTT_PORT) : MQTT_PORT;

	buf = calloc(sep - addr + 1, 1);
	if (!buf) {
		fprintf(stderr, "ERROR: Out of memory\n");
		return;
	}

	strncpy(buf, addr, sep - addr);

	err = __add_broker(buf, port, bind_addr, id, topic_prefix);
	if (err)
		free(buf);
}

void
mqtt_broker_add(const char *addr, int port, const char *bind_addr, const char *id,
				const char *topic_prefix)
{
	char *buf;
	size_t addrlen;
	int err;

	err = validate_topic(topic_prefix, id);
	if (err)
		return;

	addrlen = strlen(addr);
	buf = calloc(addrlen + 1, 1);
	if (!buf) {
		fprintf(stderr, "ERROR: Out fo memory\n");
		return;
	}

	strncpy(buf, addr, addrlen);
	__add_broker(buf, port, bind_addr, id, topic_prefix);
}

static int
mqtt_connect(struct mqtt_context *ctx, bool reconnect)
{
	int err;

	if (reconnect)
		err = mosquitto_reconnect(ctx->mosq);
	else
		err = mosquitto_connect_bind_v5(ctx->mosq, ctx->addr, ctx->port, KEEPALIVE_SECONDS,
										ctx->bind_addr, NULL);

	if (err) {
		fprintf(stderr, "%s: connecting to %s:%d via %s failed: %s\n",
			ctx->id, ctx->addr, ctx->port, ctx->bind_addr,
			err == MOSQ_ERR_ERRNO ? strerror(errno) : mosquitto_strerror(err));
		return err;
	}

	list_move_tail(&ctx->list, &brokers);
	return 0;
}

static int
mqtt_publish(struct mqtt_context *ctx, const char *topic, const void *data, int len, bool api,
			 bool retain, int qos, mosquitto_property *props)
{
	static char buf[TOPIC_MAXLEN + 1];
	int chars;

	chars = snprintf(buf, TOPIC_MAXLEN, api ? "%s%s/api/%s" : "%s%s/%s",
		ctx->topic_prefix ? ctx->topic_prefix : "",
		ctx->id, topic);

	if (chars >= TOPIC_MAXLEN) {
		fprintf(stderr, "Topic exceeds maximum length of %d\n", TOPIC_MAXLEN);
		return -1;
	}

	return mosquitto_publish_v5(ctx->mosq, NULL, buf, len, data, qos, retain, props);
}

static inline int
mqtt_publish_api(struct mqtt_context *ctx, const char *topic, const void *data, int len,
				 mosquitto_property *props)
{
	return mqtt_publish(ctx, topic, data, len, true, true, 1, props);
}

static int
phy_dump_cb(void *arg, char *buf)
{
	struct mqtt_context *ctx = arg;
	char *newline, *sep;

	sep = strchr(buf, ';');
	if (!sep)
		return -1;

	*sep++ = '\0';
	newline = strrchr(sep, '\n');
	if (newline)
		*newline = '\0';

	if (*buf == '#') {
		buf++;
		return mqtt_publish_api(ctx, buf, sep, strlen(sep), NULL);
	}

	return mqtt_publish(ctx, buf, sep, strlen(sep), false, true, 1, NULL);
}

void
mqtt_phy_event(struct phy *phy, const char *str)
{
	static char topic_buf[16];
	const char *timestamp;
	char *topic_ptr, *tmp, ev_buf[128];

	struct mqtt_context *ctx;

	list_for_each_entry(ctx, &brokers, list) {
		timestamp = str;
		tmp = strchr(str, ';');
		if (!tmp)
			return;
		*tmp = '\0';

		topic_ptr = ++tmp;
		tmp = strchr(tmp, ';');
		if (!topic_ptr)
			return;
		*tmp = '\0';

		snprintf(topic_buf, 16, "%s/%s", phy_name(phy), topic_ptr);
		snprintf(ev_buf, 128, "%s;%s", timestamp, ++tmp);

		mqtt_publish(ctx, topic_buf, ev_buf, strlen(ev_buf), false, false, 0, NULL);
	}
}

static inline int
mqtt_phy_publish(struct mqtt_context *ctx, struct phy *phy, bool add)
{
	return mqtt_publish(ctx, phy_name(phy),
						add ? "0;add" : "0;remove",
						add ? 5 : 8,
						false, true, 1, NULL);
}

static void
mqtt_set_phy_state(struct mqtt_context *ctx, struct phy *phy, bool add)
{
	if (!ctx) {
		list_for_each_entry(ctx, &brokers, list)
			mqtt_set_phy_state(ctx, phy, add);
		return;
	}

	if (add && !ctx->init_done) {
		mqtt_phy_dump(phy, phy_dump_cb, ctx);
		ctx->init_done = true;
	}

	mqtt_phy_publish(ctx, phy, add);
}

static void
on_connect(struct mosquitto *mosq, void *arg, int rc, int flags, const mosquitto_property *prop)
{
	enum {
		CONN_REFUSED_INVALID_PROTO = 1,
		CONN_REFUSED_ID_REJECTED = 2,
		CONN_REFUSED_BROKER_UNAVAIL = 3,
		__CONN_MAX
	};

	static const char *rc_str[__CONN_MAX] = {
		[CONN_REFUSED_INVALID_PROTO] = "invalid protocol",
		[CONN_REFUSED_ID_REJECTED] = "ID rejected",
		[CONN_REFUSED_BROKER_UNAVAIL] = "broker unavailable",
	};

	struct phy *phy;
	struct mqtt_context *ctx = arg;

	if (rc) {
		fprintf(stderr, "%s:%s:%d > Connection refused: %s",
			ctx->id, ctx->addr, ctx->port, rc_str[rc]);
		return;
	}

	vlist_for_each_element(&phy_list, phy, node)
		mqtt_set_phy_state(ctx, phy, true);
}

static void
on_disconnect(struct mosquitto *mosq, void *arg, int reason, const mosquitto_property *prop)
{
	bool others_pending;
	struct mqtt_context *ctx = arg;

	fprintf(stderr, "%s lost connection to %s:%d\n", ctx->id, ctx->addr, ctx->port);

	uloop_fd_delete(&ctx->fd);

	others_pending = !list_empty(&pending);
	list_add_tail(&ctx->list, &pending);
	if (!others_pending)
		uloop_timeout_set(&restart_timer, 100);
}

static void
mqtt_handle_sock_event(struct uloop_fd *fd, unsigned int events)
{
	struct mqtt_context *ctx = container_of(fd, struct mqtt_context, fd);

	if (events & ULOOP_READ)
		mosquitto_loop_read(ctx->mosq, 1);
}

static void
mqtt_connect_pending(struct uloop_timeout *timeout)
{
	static const char *will_msg = "disconnected";
	struct mqtt_context *ctx, *tmp;
	bool reconnect;
	int err;

	list_for_each_entry_safe(ctx, tmp, &pending, list) {
		if (!ctx->mosq) {
			reconnect = false;
			ctx->mosq = mosquitto_new(ctx->id, false, ctx);
			if (!ctx->mosq) {
				fprintf(stderr, "%s:%s:%d > Failed to initialize mosquitto client: %s\n",
					ctx->id, ctx->addr, ctx->port, strerror(errno));
				exit(errno);
			}
			mosquitto_connect_v5_callback_set(ctx->mosq, on_connect);
			mosquitto_disconnect_v5_callback_set(ctx->mosq, on_disconnect);
		} else {
			reconnect = true;
		}

		err = mosquitto_will_set_v5(ctx->mosq, ctx->id, strlen(will_msg), will_msg, 0, false, NULL);
		if (err)
			fprintf(stderr, "%s:%s:%d WARNING: setting will failed: %s\n",
					ctx->id, ctx->addr, ctx->port, mosquitto_strerror(err));

		err = mqtt_connect(ctx, reconnect);
		if (err)
			continue;

		ctx->fd.fd = mosquitto_socket(ctx->mosq);
		ctx->fd.cb = mqtt_handle_sock_event;
		uloop_fd_add(&ctx->fd, ULOOP_READ);
	}

	if (!list_empty(&pending))
		uloop_timeout_set(timeout, 100);
}

static void
mqtt_context_destroy(struct mqtt_context *ctx)
{
	mosquitto_destroy(ctx->mosq);
	free(ctx->addr);
	free(ctx);
}

void
mqtt_stop(void)
{
	struct mqtt_context *ctx, *tmp;

	uloop_timeout_cancel(&restart_timer);

	list_for_each_entry_safe(ctx, tmp, &pending, list) {
		list_del(&ctx->list);
		mqtt_context_destroy(ctx);
	}

	list_for_each_entry_safe(ctx, tmp, &brokers, list) {
		list_del(&ctx->list);
		mosquitto_disconnect_v5(ctx->mosq, -1, NULL);
		mqtt_context_destroy(ctx);
	}

	mosquitto_lib_cleanup();
}

static void
mqtt_maintain(struct uloop_timeout *timeout)
{
	struct mqtt_context *ctx;
	int err;

	list_for_each_entry(ctx, &brokers, list) {
		err = mosquitto_loop_misc(ctx->mosq);
		switch (err) {
			case MOSQ_ERR_INVAL:
				fprintf(stderr, "%s:%s:%d > error during periodic maintenance of mqtt connection\n",
					ctx->id, ctx->addr, ctx->port);
					break;
			case MOSQ_ERR_NO_CONN:
				mqtt_connect(ctx, true);
				break;
		}
	}

	uloop_timeout_set(timeout, MOSQUITTO_MAINTENANCE_FREQ_MS);
}

void
mqtt_init()
{
	mosquitto_lib_init();

	restart_timer.cb = mqtt_connect_pending;
	mqtt_connect_pending(&restart_timer);

	mosquitto_misc_timer.cb = mqtt_maintain;
	uloop_timeout_set(&mosquitto_misc_timer, MOSQUITTO_MAINTENANCE_FREQ_MS);
}

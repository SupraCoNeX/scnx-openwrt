#include <uci.h>

#include "rcd.h"

static struct uci_context *uci_ctx = NULL;
static struct uci_package *config = NULL;

#ifdef CONFIG_MQTT
const char *global_id = NULL;
const char *global_topic = NULL;
const char *capath = "/etc/ssl/certs/";
#endif

static struct uci_package*
config_init_package(const char *config)
{
	struct uci_context *ctx = uci_ctx;
	struct uci_package *p;

	if (!ctx) {
		ctx = uci_alloc_context();
		uci_ctx = ctx;

		ctx->flags &= ~UCI_FLAG_STRICT;
		if (config_path)
			uci_set_confdir(ctx, config_path);
	} else {
		p = uci_lookup_package(ctx, config);
		if (p)
			uci_unload(ctx, p);
	}

	if (uci_load(ctx, config, &p))
		return NULL;

	return p;
}

#ifdef CONFIG_MQTT
static void
config_init_globals(void)
{
	const char *tmp;
	struct uci_section *globals = uci_lookup_section(uci_ctx, config, "rcd");
	if (globals) {
		global_id = uci_lookup_option_string(uci_ctx, globals, "id");
		global_topic = uci_lookup_option_string(uci_ctx, globals, "topic");
		tmp = uci_lookup_option_string(uci_ctx, globals, "capath");
		if (tmp)
			capath = tmp;
	}
}

static void
config_parse_mqtt_broker(struct uci_section *s)
{
	const char *bind_addr, *id, *addr, *topic, *portstr;
	int port;

	addr = uci_lookup_option_string(uci_ctx, s, "addr");
	if (!addr)
		return;

	portstr = uci_lookup_option_string(uci_ctx, s, "port");
	port = portstr ? atoi(portstr) : MQTT_PORT;

	id = uci_lookup_option_string(uci_ctx, s, "id");
	if (!id)
		id = global_id;
	if (!id)
		return;

	bind_addr = uci_lookup_option_string(uci_ctx, s, "bindaddr");
	topic = uci_lookup_option_string(uci_ctx, s, "topic");
	if (!topic)
		topic = global_topic;

	mqtt_broker_add(addr, port, bind_addr, id, topic, capath);
}

static void
config_init_mqtt(void)
{
	struct uci_element *e;
	uci_foreach_element(&config->sections, e) {
		struct uci_section *s = uci_to_section(e);

		if (strcmp(s->type, "mqtt") == 0)
			config_parse_mqtt_broker(s);
	}
}
#endif

#ifdef CONFIG_ZSTD
static void
config_parse_zstd(struct uci_section *s, struct zstd_opts *o)
{
	const char *tmp;

	o->dict = uci_lookup_option_string(uci_ctx, s, "dict");

	tmp = uci_lookup_option_string(uci_ctx, s, "compression_level");
	if (tmp)
		o->comp_level = atoi(tmp);

	tmp = uci_lookup_option_string(uci_ctx, s, "buffer_size");
	if (tmp)
		o->bufsize = atoi(tmp);

	tmp = uci_lookup_option_string(uci_ctx, s, "timeout_ms");
	if (tmp)
		o->timeout_ms = atoi(tmp);
}

void
config_init_zstd(struct zstd_opts *o)
{
	struct uci_element *e;
	uci_foreach_element(&config->sections, e) {
		struct uci_section *s = uci_to_section(e);

		if (strcmp(s->type, "zstd") == 0) {
			config_parse_zstd(s, o);
			break;
		}
	}
}
#endif

void
rcd_config_init(void)
{
	char *errstr;

	config = config_init_package("minstrel-rcd");
	if (!config && uci_ctx->err != UCI_ERR_NOTFOUND) {
		uci_get_errorstr(uci_ctx, &errstr, NULL);
		fprintf(stderr, "ERROR: Failed to load config: %s\n", errstr);
		free(errstr);
		return;
	}

#ifdef CONFIG_MQTT
	config_init_globals();
	config_init_mqtt();
#endif
}
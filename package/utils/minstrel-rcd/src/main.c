// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021 Felix Fietkau <nbd@nbd.name> */
#include <libubox/usock.h>
#include <libubox/ustream.h>
#include "rcd.h"

const char *config_path = NULL; /* use the default set in libuci */

static void
usage(void)
{
	fprintf(stderr, "usage: minstrel-rcd [-h INTERFACE]");
#ifdef CONFIG_MQTT
	fprintf(stderr, " [-i ID] [-t TOPIC_PREFIX] [-b BROKER]");
#endif
#ifdef CONFIG_ZSTD
	fprintf(stderr, " [-D DICT] [-c COMPRESSIONLEVEL] [-B BUFSIZE] [-T TIMEOUT_MS]");
#endif
	fprintf(stderr, "\n");

#ifdef CONFIG_MQTT
	fprintf(stderr, "MQTT options: [-i ID] [-t TOPIC_PREFIX] [-b BROKER]\n"
			"       ID is used to identify with the broker,\n"
			"       TOPIC_PREFIX is prepended to all mqtt messages published by this node, and\n"
			"       BROKER is ADDRESS[:PORT] for IPv4 and '[ADDRESS]'[:PORT] for IPv6\n"
			"       Note: You may connect to multiple brokers re-using all options expcept BROKER.\n"
			"             Simply provide them before specifying the broker.\n");
#endif

#ifdef CONFIG_ZSTD
	fprintf(stderr, "zstd compression options: [-D DICT] [-c COMPRESSIONLEVEL] [-B BUFSIZE] [-T TIMEOUT_MS]\n"
			"	DICT is the path to a zstd dictionary file (default /lib/minstrel-rcd/dictionary/zdict)\n"
			"	COMPRESSIONLEVEL sets the zstd compression level (default 3)\n"
			"	BUFSIZE sets the size of the buffer where data is collected before compression (default 4096)\n"
			"	TIMEOUT_MS sets the maximum wait time in milliseconds between flushes of the compression buffer (default 1000).\n");
#endif
}

static void
rcd_stop(int signo)
{
	static bool stopped = false;

	if (stopped)
		return;

#ifdef CONFIG_ZSTD
	zstd_stop(true);
#endif
#ifdef CONFIG_MQTT
	mqtt_stop();
#endif
	uloop_end();

	stopped = true;
}

static void
rcd_setup_signals(void)
{
	struct sigaction s;

	memset(&s, 0, sizeof(s));
	s.sa_handler = rcd_stop;
	s.sa_flags = 0;
	sigaction(SIGINT, &s, NULL);
	sigaction(SIGTERM, &s, NULL);
	sigaction(SIGUSR1, &s, NULL);
	sigaction(SIGUSR2, &s, NULL);

	s.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &s, NULL);
}

int main(int argc, char **argv)
{
	int ch;

#ifdef CONFIG_MQTT
	const char *bind_addr = NULL;
	const char *mqtt_id = NULL;
	const char *topic = NULL;
	const char *capath = "/etc/ssl/certs/";
#endif

	uloop_init();
	rcd_config_init();

#ifdef CONFIG_ZSTD
	struct zstd_opts zstdopts = ZSTD_OPTS_DEFAULTS;
	config_init_zstd(&zstdopts);
#endif

	while ((ch = getopt(argc, argv, "h:i:C:b:t:D:c:B:T:")) != -1) {
		switch (ch) {
		case 'h':
			rcd_server_add(optarg);
#ifdef CONFIG_MQTT
			bind_addr = optarg;
			break;
		case 'i':
			mqtt_id = optarg;
			break;
		case 'C':
			capath = optarg;
			break;
		case 't':
			topic = optarg;
			break;
		case 'b':
			mqtt_broker_add_cli(optarg, bind_addr, mqtt_id, topic, capath);
#endif
			break;
#ifdef CONFIG_ZSTD
		case 'D':
			zstdopts.dict = optarg;
			break;
		case 'c':
			zstdopts.comp_level = atoi(optarg);
			break;
		case 'B':
			zstdopts.bufsize = atoi(optarg);
			break;
		case 'T':
			zstdopts.timeout_ms = atoi(optarg);
			break;
#endif
		default:
			usage();
			exit(1);
		}
	}

	rcd_setup_signals();

#ifdef CONFIG_ZSTD
	if(zstd_init(&zstdopts)) {
		uloop_end();
		return -1;
	}
#endif

	rcd_phy_init();
	rcd_server_init();
#ifdef CONFIG_MQTT
	mqtt_init();
#endif

	printf("ready to accept connections...\n");

	uloop_run();

	rcd_stop(-1);

	return 0;
}

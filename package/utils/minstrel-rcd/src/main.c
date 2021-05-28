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
	fprintf(stderr, " [-i ID] [-t TOPIC_PREFIX] [-b BROKER]\n"
					"       where ID is used to identify with the broker,\n"
					"       TOPIC_PREFIX is prepended to all mqtt messages published by this node, and\n"
					"       BROKER is ADDRESS[:PORT] for IPv4 and '[ADDRESS]'[:PORT] for IPv6\n"
					"       Note: You may connect to multiple brokers re-using all options expcept BROKER.\n"
					"             Simply provide them before specifying the broker.\n");
#endif
	fprintf(stderr, "\n");
}

static void
rcd_stop(int signo)
{
#ifdef CONFIG_MQTT
	mqtt_stop();
#endif
	uloop_end();
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
#endif

	uloop_init();

#ifdef CONFIG_MQTT
	rcd_config_init();
#endif

	while ((ch = getopt(argc, argv, "h:i:b:t:")) != -1) {
		switch (ch) {
		case 'h':
			rcd_server_add(optarg);
#ifdef CONFIG_MQTT
			bind_addr = optarg;
			break;
		case 'i':
			mqtt_id = optarg;
			break;
		case 't':
			topic = optarg;
			break;
		case 'b':
			mqtt_broker_add_cli(optarg, bind_addr, mqtt_id, topic);
#endif
			break;
		default:
			usage();
			exit(1);
		}
	}

	rcd_setup_signals();

	rcd_phy_init();
	rcd_server_init();
#ifdef CONFIG_MQTT
	mqtt_init();
#endif

	uloop_run();

	rcd_stop(-1);

	return 0;
}

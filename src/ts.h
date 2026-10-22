/*
 * luci-app-traffic-stats - shared declarations
 *
 * Copyright (C) 2026 luci-app-traffic-stats contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef TS_H
#define TS_H

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define TS_VERSION "1.0.1"

#define MAX_IFACES      8
#define MAX_ALIASES     64
#define MAX_DEVICES     512
#define MAX_DESTINATIONS 8192
#define MAX_DNS_ENTRIES 4096
#define MAX_HISTORY_IFACES 8

/* capture roles */
#define ROLE_LAN 0
#define ROLE_WAN 1

/* device table */
struct device {
	uint8_t  mac[6];
	uint32_t ip;              /* last seen IPv4 address (network order) */
	uint64_t up;              /* bytes uploaded to the internet */
	uint64_t down;            /* bytes downloaded from the internet */
	uint64_t up_int;          /* bytes exchanged inside the LAN */
	uint64_t down_int;
	uint64_t up_prev, down_prev;
	uint64_t up_prev_h, down_prev_h;
	uint64_t up_prev_d, down_prev_d;
	uint64_t up_rate, down_rate;
	uint64_t up6, down6;      /* IPv6 payload bytes */
	time_t   last;
	int      used;
};

/* destination table (external peers, IPv4) */
struct destination {
	uint32_t ip;
	uint64_t up;              /* bytes sent to this peer */
	uint64_t down;            /* bytes received from this peer */
	uint64_t up_pkts;         /* packets sent to this peer */
	uint64_t down_pkts;       /* packets received from this peer */
	uint64_t up_prev, down_prev;
	uint64_t up_prev_d, down_prev_d;
	uint64_t up_pkts_prev_d, down_pkts_prev_d;
	uint64_t up_rate, down_rate;
	time_t   last;
	int      used;
};

/* domain cache, filled from DNS responses seen on the wire */
struct dns_entry {
	uint32_t ip;
	char     domain[192];
	time_t   expiry;
	int      used;
};

struct alias {
	char mac[18];
	char name[64];
};

/* local IPv4 prefixes used to tell "inside" from "outside" traffic */
struct prefix4 {
	uint32_t addr;
	uint32_t mask;
};

/* local IPv6 prefixes */
struct prefix6 {
	uint8_t addr[16];
	int     bits;
};

struct config {
	char lan_ifaces[MAX_IFACES][32];
	int  lan_count;
	char wan_ifaces[MAX_IFACES][32];
	int  wan_count;
	char stats_iface[32];      /* interface used for /proc/net/dev counters */
	char runtime_dir[128];
	char history_dir[128];
	char lease_file[128];
	int  sample_interval;      /* seconds between snapshots */
	int  dns_ttl;              /* seconds a domain mapping stays valid */
	int  count_internal;       /* also account for LAN internal traffic */
	int  history_days;
	int  verbose;
	struct alias aliases[MAX_ALIASES];
	int  alias_count;
};

struct state {
	struct config cfg;
	pthread_mutex_t lock;

	struct device devices[MAX_DEVICES];
	struct destination dests[MAX_DESTINATIONS];
	struct dns_entry dns[MAX_DNS_ENTRIES];

	/* global counters */
	uint64_t wan_up, wan_down;         /* external payload bytes seen on wan */
	uint64_t wan_up_prev, wan_down_prev;
	uint64_t wan_up_rate, wan_down_rate;
	uint64_t lan_up, lan_down;         /* external payload bytes seen on lan */
	uint64_t ipv6_up, ipv6_down;
	uint64_t net_rx_bytes, net_tx_bytes;  /* /proc/net/dev counters */
	uint64_t net_rx_prev, net_tx_prev;
	uint64_t net_rx_rate, net_tx_rate;

	uint64_t packets, bytes, errors, dropped;
	uint64_t pkts_up, pkts_down, pkts_int, pkts_unknown;
	uint64_t pkts_debug[2];
	uint64_t dns_queries, dns_responses;

	time_t start_time;
	time_t last_minute, last_hour, last_day;
};

/* capture.c */
int  capture_open(struct state *st);
void capture_run(struct state *st);
void capture_close(struct state *st);

/* hash.c */
void  devices_init(struct state *st);
struct device *device_get(struct state *st, const uint8_t *mac, uint32_t ip);
struct destination *dest_get(struct state *st, uint32_t ip);
struct dns_entry *dns_lookup(struct state *st, uint32_t ip);
void  dns_learn(struct state *st, uint32_t ip, const char *domain, time_t expiry);
void  dns_expire(struct state *st);

/* local.c */
int  local_init(struct state *st);
int  is_local4(struct state *st, uint32_t ip);
int  is_local6(struct state *st, const uint8_t *addr);
const char *device_name(struct state *st, const uint8_t *mac, uint32_t ip, char *buf, size_t len);
void local_refresh(struct state *st);

/* dns.c */
int  dns_parse_packet(struct state *st, const uint8_t *payload, size_t len,
                       int is_tcp, int from_client);

/* store.c */
int  store_init(struct state *st);
void store_tick(struct state *st);
void store_prune(struct state *st);

/* json.c */
int  write_snapshot(struct state *st, const char *path);

/* util.c */
uint32_t ts_now(void);
void     ts_set_ifnames(struct state *st);
int      ts_load_config(struct state *st);
void     ts_log(const char *fmt, ...);

#endif /* TS_H */

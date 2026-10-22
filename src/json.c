/*
 * luci-app-traffic-stats - runtime snapshot writer
 *
 * The current state is written as a single JSON document. The LuCI
 * application reads that file through the rpcd plugin, so the web interface
 * never has to talk to the capture thread directly.
 */

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ts.h"

#define MAX_REPORT_DEVICES 128
#define MAX_REPORT_DESTS   200
#define MAX_REPORT_DOMAINS 200

struct dev_ref {
	const struct device *dev;
};

struct dst_ref {
	const struct destination *dst;
	const char *domain;
};

struct dom_ref {
	char     domain[192];
	uint64_t up;
	uint64_t down;
};

static int cmp_dev(const void *a, const void *b)
{
	const struct dev_ref *x = a;
	const struct dev_ref *y = b;
	uint64_t xs = x->dev->up + x->dev->down;
	uint64_t ys = y->dev->up + y->dev->down;

	if (xs != ys)
		return (xs < ys) ? 1 : -1;

	return 0;
}

static int cmp_dst(const void *a, const void *b)
{
	const struct dst_ref *x = a;
	const struct dst_ref *y = b;
	uint64_t xs = x->dst->up + x->dst->down;
	uint64_t ys = y->dst->up + y->dst->down;

	if (xs != ys)
		return (xs < ys) ? 1 : -1;

	return 0;
}

static int cmp_dom(const void *a, const void *b)
{
	const struct dom_ref *x = a;
	const struct dom_ref *y = b;
	uint64_t xs = x->up + x->down;
	uint64_t ys = y->up + y->down;

	if (xs != ys)
		return (xs < ys) ? 1 : -1;

	return 0;
}

static void json_escape(FILE *fp, const char *s)
{
	fputc('"', fp);

	for (; s != NULL && *s != '\0'; s++) {
		unsigned char c = (unsigned char)*s;

		switch (c) {
		case '"':
			fputs("\\\"", fp);
			break;
		case '\\':
			fputs("\\\\", fp);
			break;
		case '\n':
			fputs("\\n", fp);
			break;
		case '\r':
			fputs("\\r", fp);
			break;
		case '\t':
			fputs("\\t", fp);
			break;
		default:
			if (c < 0x20)
				fprintf(fp, "\\u%04x", c);
			else
				fputc(c, fp);
		}
	}

	fputc('"', fp);
}

int write_snapshot(struct state *st, const char *path)
{
	char tmp[256];
	FILE *fp;
	/* every table entry is ranked before the report is cut down */
	static struct dev_ref devs[MAX_DEVICES];
	static struct dst_ref dsts[MAX_DESTINATIONS];
	static struct dom_ref doms[MAX_REPORT_DOMAINS];
	int ndev = 0, ndst = 0, ndom = 0;
	int i, j;
	uint64_t dev_up = 0, dev_down = 0;
	uint64_t up, down;
	char ipbuf[32];

	snprintf(tmp, sizeof(tmp), "%s.new", path);

	fp = fopen(tmp, "w");
	if (fp == NULL)
		return -1;

	/* devices, the table order must not decide who is reported */
	for (i = 0; i < MAX_DEVICES; i++) {
		if (!st->devices[i].used)
			continue;
		if (st->devices[i].up == 0 && st->devices[i].down == 0 &&
		    st->devices[i].up_int == 0 && st->devices[i].down_int == 0)
			continue;

		dev_up += st->devices[i].up;
		dev_down += st->devices[i].down;
		devs[ndev].dev = &st->devices[i];
		ndev++;
	}
	qsort(devs, ndev, sizeof(devs[0]), cmp_dev);

	if (ndev > MAX_REPORT_DEVICES)
		ndev = MAX_REPORT_DEVICES;

	/* destinations */
	for (i = 0; i < MAX_DESTINATIONS; i++) {
		struct dns_entry *e;

		if (!st->dests[i].used)
			continue;
		if (st->dests[i].up == 0 && st->dests[i].down == 0)
			continue;

		dsts[ndst].dst = &st->dests[i];
		e = dns_lookup(st, st->dests[i].ip);
		dsts[ndst].domain = (e != NULL) ? e->domain : NULL;
		ndst++;
	}
	qsort(dsts, ndst, sizeof(dsts[0]), cmp_dst);

	/* domains, the summary uses every destination */
	for (i = 0; i < ndst; i++) {
		const char *dom = dsts[i].domain;

		if (dom == NULL || dom[0] == '\0')
			continue;

		for (j = 0; j < ndom; j++) {
			if (strcmp(doms[j].domain, dom) == 0) {
				doms[j].up += dsts[i].dst->up;
				doms[j].down += dsts[i].dst->down;
				break;
			}
		}

		if (j == ndom && ndom < MAX_REPORT_DOMAINS) {
			snprintf(doms[ndom].domain, sizeof(doms[ndom].domain), "%s", dom);
			doms[ndom].up = dsts[i].dst->up;
			doms[ndom].down = dsts[i].dst->down;
			ndom++;
		}
	}
	qsort(doms, ndom, sizeof(doms[0]), cmp_dom);

	if (ndst > MAX_REPORT_DESTS)
		ndst = MAX_REPORT_DESTS;

	if (capture_wan_active()) {
		up = st->wan_up;
		down = st->wan_down;
	} else {
		up = st->lan_up;
		down = st->lan_down;
	}

	fprintf(fp, "{\n");
	fprintf(fp, "  \"version\": \"%s\",\n", TS_VERSION);
	fprintf(fp, "  \"ts\": %u,\n", (unsigned int)ts_now());
	fprintf(fp, "  \"uptime\": %u,\n", (unsigned int)(ts_now() - st->start_time));
	fprintf(fp, "  \"interval\": %d,\n", st->cfg.sample_interval);

	fprintf(fp, "  \"wan\": {");
	fprintf(fp, "\"iface\": ");
	json_escape(fp, st->cfg.stats_iface);
	fprintf(fp, ", \"rx_bytes\": %llu, \"tx_bytes\": %llu",
		(unsigned long long)st->net_rx_bytes, (unsigned long long)st->net_tx_bytes);
	fprintf(fp, ", \"rx_rate\": %llu, \"tx_rate\": %llu",
		(unsigned long long)st->net_rx_rate, (unsigned long long)st->net_tx_rate);
	fprintf(fp, ", \"up_bytes\": %llu, \"down_bytes\": %llu",
		(unsigned long long)up, (unsigned long long)down);
	fprintf(fp, ", \"up_rate\": %llu, \"down_rate\": %llu",
		(unsigned long long)st->wan_up_rate, (unsigned long long)st->wan_down_rate);
	fprintf(fp, " },\n");

	fprintf(fp, "  \"router\": {");
	fprintf(fp, "\"up_bytes\": %llu, \"down_bytes\": %llu",
		(unsigned long long)(up > dev_up ? up - dev_up : 0),
		(unsigned long long)(down > dev_down ? down - dev_down : 0));
	fprintf(fp, " },\n");

	fprintf(fp, "  \"devices\": [");
	for (i = 0; i < ndev; i++) {
		const struct device *d = devs[i].dev;
		char name[128];
		char mac[18];

		snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
			 d->mac[0], d->mac[1], d->mac[2], d->mac[3], d->mac[4], d->mac[5]);
		device_name(st, d->mac, d->ip, name, sizeof(name));

		if (d->ip != 0)
			ts_fmt_ip(d->ip, ipbuf, sizeof(ipbuf));
		else
			ipbuf[0] = '\0';

		fprintf(fp, "%s\n    {", (i > 0) ? "," : "");
		fprintf(fp, "\"mac\": ");
		json_escape(fp, mac);
		fprintf(fp, ", \"ip\": ");
		json_escape(fp, ipbuf);
		fprintf(fp, ", \"name\": ");
		json_escape(fp, name);
		fprintf(fp, ", \"up\": %llu, \"down\": %llu",
			(unsigned long long)d->up, (unsigned long long)d->down);
		fprintf(fp, ", \"up_h\": %llu, \"down_h\": %llu",
			(unsigned long long)(d->up - d->up_prev_h),
			(unsigned long long)(d->down - d->down_prev_h));
		fprintf(fp, ", \"up_rate\": %llu, \"down_rate\": %llu",
			(unsigned long long)d->up_rate, (unsigned long long)d->down_rate);
		fprintf(fp, ", \"internal_up\": %llu, \"internal_down\": %llu",
			(unsigned long long)d->up_int, (unsigned long long)d->down_int);
		fprintf(fp, ", \"last\": %u}", (unsigned int)d->last);
	}
	fprintf(fp, "%s],\n", (ndev > 0) ? "\n  " : "");

	fprintf(fp, "  \"destinations\": [");
	for (i = 0; i < ndst; i++) {
		ts_fmt_ip(dsts[i].dst->ip, ipbuf, sizeof(ipbuf));

		fprintf(fp, "%s\n    {", (i > 0) ? "," : "");
		fprintf(fp, "\"ip\": ");
		json_escape(fp, ipbuf);
		fprintf(fp, ", \"domain\": ");
		if (dsts[i].domain != NULL)
			json_escape(fp, dsts[i].domain);
		else
			fprintf(fp, "null");
		fprintf(fp, ", \"up\": %llu, \"down\": %llu",
			(unsigned long long)dsts[i].dst->up, (unsigned long long)dsts[i].dst->down);
		fprintf(fp, ", \"up_h\": %llu, \"down_h\": %llu",
			(unsigned long long)(dsts[i].dst->up - dsts[i].dst->up_prev_h),
			(unsigned long long)(dsts[i].dst->down - dsts[i].dst->down_prev_h));
		fprintf(fp, ", \"up_pkts\": %llu, \"down_pkts\": %llu",
			(unsigned long long)dsts[i].dst->up_pkts,
			(unsigned long long)dsts[i].dst->down_pkts);
		fprintf(fp, ", \"up_rate\": %llu, \"down_rate\": %llu",
			(unsigned long long)dsts[i].dst->up_rate,
			(unsigned long long)dsts[i].dst->down_rate);
		fprintf(fp, ", \"last\": %u}", (unsigned int)dsts[i].dst->last);
	}
	fprintf(fp, "%s],\n", (ndst > 0) ? "\n  " : "");

	fprintf(fp, "  \"domains\": [");
	for (i = 0; i < ndom; i++) {
		fprintf(fp, "%s\n    {", (i > 0) ? "," : "");
		fprintf(fp, "\"domain\": ");
		json_escape(fp, doms[i].domain);
		fprintf(fp, ", \"up\": %llu, \"down\": %llu",
			(unsigned long long)doms[i].up, (unsigned long long)doms[i].down);
		fprintf(fp, "}");
	}
	fprintf(fp, "%s],\n", (ndom > 0) ? "\n  " : "");

	fprintf(fp, "  \"counters\": {");
	fprintf(fp, "\"packets\": %llu, \"bytes\": %llu, \"errors\": %llu, \"dropped\": %llu",
		(unsigned long long)st->packets, (unsigned long long)st->bytes,
		(unsigned long long)st->errors, (unsigned long long)st->dropped);
	fprintf(fp, ", \"up_pkts\": %llu, \"down_pkts\": %llu, \"internal_pkts\": %llu, \"unknown_pkts\": %llu",
		(unsigned long long)st->pkts_up, (unsigned long long)st->pkts_down,
		(unsigned long long)st->pkts_int, (unsigned long long)st->pkts_unknown);
	fprintf(fp, "},\n");

	fprintf(fp, "  \"dns\": {");
	fprintf(fp, "\"queries\": %llu, \"responses\": %llu, \"entries\": %d",
		(unsigned long long)st->dns_queries, (unsigned long long)st->dns_responses, ndom);
	fprintf(fp, "},\n");

	fprintf(fp, "  \"interfaces\": {");
	fprintf(fp, "\"lan\": [");
	for (i = 0; i < st->cfg.lan_count; i++) {
		if (i > 0)
			fprintf(fp, ", ");
		json_escape(fp, st->cfg.lan_ifaces[i]);
	}
	fprintf(fp, "], \"wan\": [");
	for (i = 0; i < st->cfg.wan_count; i++) {
		if (i > 0)
			fprintf(fp, ", ");
		json_escape(fp, st->cfg.wan_ifaces[i]);
	}
	fprintf(fp, "]}");

	fprintf(fp, "\n}\n");

	fclose(fp);

	if (rename(tmp, path) != 0) {
		unlink(tmp);
		return -1;
	}

	return 0;
}

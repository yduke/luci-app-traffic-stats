/*
 * luci-app-traffic-stats - history storage
 *
 * Fine grained data is kept in the ram disk, the long term statistics are
 * appended to the overlay filesystem. The number of write operations per
 * day stays low because the persistent files are only updated once per
 * hour respectively once per day.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ts.h"

#define MINUTES_TTL   (7 * 24 * 3600)
#define DEVICE_HOURS_TTL (31 * 24 * 3600)
#define TOP_DESTINATIONS 50

static void ensure_dir(const char *path)
{
	mkdir(path, 0755);
}

static void path_join(char *dst, size_t len, const char *dir, const char *file)
{
	snprintf(dst, len, "%s/%s", dir, file);
}

int store_init(struct state *st)
{
	ensure_dir(st->cfg.runtime_dir);
	ensure_dir(st->cfg.history_dir);

	return 0;
}

static void append_line(const char *dir, const char *file, const char *line)
{
	char path[256];
	FILE *fp;

	path_join(path, sizeof(path), dir, file);

	fp = fopen(path, "a");
	if (fp == NULL)
		return;

	fputs(line, fp);
	fputc('\n', fp);
	fclose(fp);
}

/*
 * Keep only the rows that are still inside the retention window. The file
 * is rewritten once per day, the flash wear is therefore negligible.
 */
static void prune_file(const char *dir, const char *file, time_t ttl)
{
	char path[192], tmp[200];
	FILE *in, *out;
	char line[512];
	time_t now = ts_now();

	path_join(path, sizeof(path), dir, file);
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);

	in = fopen(path, "r");
	if (in == NULL)
		return;

	out = fopen(tmp, "w");
	if (out == NULL) {
		fclose(in);
		return;
	}

	while (fgets(line, sizeof(line), in) != NULL) {
		char *tab = strchr(line, '\t');
		time_t ts;

		if (tab == NULL)
			continue;

		*tab = '\0';
		ts = (time_t)strtoll(line, NULL, 10);

		if (ts > 0 && now - ts <= ttl) {
			fputs(line, out);
			fputc('\t', out);
			fputs(tab + 1, out);
			fputc('\n', out);
		}
	}

	fclose(in);
	fclose(out);

	rename(tmp, path);
}

void store_prune(struct state *st)
{
	long long days = st->cfg.history_days;
	time_t long_ttl = (time_t)(days * 24 * 3600);

	prune_file(st->cfg.runtime_dir, "minutes.tsv", MINUTES_TTL);
	prune_file(st->cfg.history_dir, "hours.tsv", long_ttl);
	prune_file(st->cfg.history_dir, "days.tsv", long_ttl);
	prune_file(st->cfg.history_dir, "device_hours.tsv", DEVICE_HOURS_TTL);
	prune_file(st->cfg.history_dir, "device_days.tsv", long_ttl);
	prune_file(st->cfg.history_dir, "dest_days.tsv", long_ttl);
}

static void write_minute_row(struct state *st, time_t bucket)
{
	char line[128];
	uint64_t up = 0, down = 0;
	int i;

	for (i = 0; i < MAX_DEVICES; i++) {
		if (!st->devices[i].used)
			continue;
		up += st->devices[i].up;
		down += st->devices[i].down;
	}

	if (st->cfg.wan_count > 0) {
		up = st->wan_up;
		down = st->wan_down;
	} else {
		up = st->lan_up;
		down = st->lan_down;
	}

	snprintf(line, sizeof(line), "%lld\t%llu\t%llu", (long long)bucket,
		 (unsigned long long)up, (unsigned long long)down);

	append_line(st->cfg.runtime_dir, "minutes.tsv", line);
}

static void write_hour_row(struct state *st, time_t bucket)
{
	char line[128];
	uint64_t up = 0, down = 0;
	int i;

	if (st->cfg.wan_count > 0) {
		up = st->wan_up;
		down = st->wan_down;
	} else {
		up = st->lan_up;
		down = st->lan_down;
	}

	snprintf(line, sizeof(line), "%lld\t%llu\t%llu", (long long)bucket,
		 (unsigned long long)up, (unsigned long long)down);
	append_line(st->cfg.history_dir, "hours.tsv", line);

	/* per device hourly deltas */
	for (i = 0; i < MAX_DEVICES; i++) {
		struct device *d = &st->devices[i];
		char mac[18];
		uint64_t dup, ddown;

		if (!d->used)
			continue;

		dup = d->up - d->up_prev_h;
		ddown = d->down - d->down_prev_h;
		d->up_prev_h = d->up;
		d->down_prev_h = d->down;

		if (dup == 0 && ddown == 0)
			continue;

		snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
			 d->mac[0], d->mac[1], d->mac[2], d->mac[3], d->mac[4], d->mac[5]);

		snprintf(line, sizeof(line), "%lld\t%s\t%u\t%llu\t%llu", (long long)bucket,
			 mac, d->ip, (unsigned long long)dup, (unsigned long long)ddown);
		append_line(st->cfg.history_dir, "device_hours.tsv", line);
	}
}

static void write_day_row(struct state *st, time_t bucket)
{
	char line[320];
	uint64_t up = 0, down = 0;
	int i, j;

	if (st->cfg.wan_count > 0) {
		up = st->wan_up;
		down = st->wan_down;
	} else {
		up = st->lan_up;
		down = st->lan_down;
	}

	snprintf(line, sizeof(line), "%lld\t%llu\t%llu", (long long)bucket,
		 (unsigned long long)up, (unsigned long long)down);
	append_line(st->cfg.history_dir, "days.tsv", line);

	for (i = 0; i < MAX_DEVICES; i++) {
		struct device *d = &st->devices[i];
		char mac[18];
		uint64_t dup, ddown;

		if (!d->used)
			continue;

		dup = d->up - d->up_prev_d;
		ddown = d->down - d->down_prev_d;
		d->up_prev_d = d->up;
		d->down_prev_d = d->down;

		if (dup == 0 && ddown == 0)
			continue;

		snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
			 d->mac[0], d->mac[1], d->mac[2], d->mac[3], d->mac[4], d->mac[5]);

		snprintf(line, sizeof(line), "%lld\t%s\t%u\t%llu\t%llu", (long long)bucket,
			 mac, d->ip, (unsigned long long)dup, (unsigned long long)ddown);
		append_line(st->cfg.history_dir, "device_days.tsv", line);
	}

	/* keep the busiest destinations of the day */
	{
		struct {
			uint32_t ip;
			uint64_t up, down, pkts;
		} top[TOP_DESTINATIONS];
		int n = 0;

		for (i = 0; i < MAX_DESTINATIONS; i++) {
			struct destination *d = &st->dests[i];
			uint64_t dup, ddown, dpkts, total;

			if (!d->used)
				continue;

			dup = d->up - d->up_prev_d;
			ddown = d->down - d->down_prev_d;
			dpkts = (d->up_pkts - d->up_pkts_prev_d) + (d->down_pkts - d->down_pkts_prev_d);
			total = dup + ddown;

			if (total == 0)
				continue;

			if (n < TOP_DESTINATIONS) {
				top[n].ip = d->ip;
				top[n].up = dup;
				top[n].down = ddown;
				top[n].pkts = dpkts;
				n++;
			} else {
				int min = 0;
				for (j = 1; j < n; j++)
					if (top[j].up + top[j].down < top[min].up + top[min].down)
						min = j;
				if (total > top[min].up + top[min].down) {
					top[min].ip = d->ip;
					top[min].up = dup;
					top[min].down = ddown;
					top[min].pkts = dpkts;
				}
			}
		}

		for (i = 0; i < MAX_DESTINATIONS; i++) {
			struct destination *d = &st->dests[i];

			if (d->used) {
				d->up_prev_d = d->up;
				d->down_prev_d = d->down;
				d->up_pkts_prev_d = d->up_pkts;
				d->down_pkts_prev_d = d->down_pkts;
			}
		}

		for (i = 0; i < n; i++) {
			struct dns_entry *e = dns_lookup(st, top[i].ip);
			char ip[24];

			snprintf(ip, sizeof(ip), "%u", top[i].ip);
			snprintf(line, sizeof(line), "%lld\t%s\t%s\t%llu\t%llu\t%llu",
				 (long long)bucket, ip, (e != NULL) ? e->domain : "",
				 (unsigned long long)top[i].up, (unsigned long long)top[i].down,
				 (unsigned long long)top[i].pkts);
			append_line(st->cfg.history_dir, "dest_days.tsv", line);
		}
	}
}

void store_tick(struct state *st)
{
	time_t now = ts_now();

	if (st->last_minute == 0)
		st->last_minute = now - 59;

	if (st->last_hour == 0)
		st->last_hour = now - 3599;

	if (st->last_day == 0)
		st->last_day = now - 86399;

	if (now - st->last_minute >= 60) {
		st->last_minute = now - (now % 60);
		write_minute_row(st, st->last_minute);
	}

	if (now - st->last_hour >= 3600) {
		st->last_hour = now - (now % 3600);
		write_hour_row(st, st->last_hour);
	}

	if (now - st->last_day >= 86400) {
		st->last_day = now - (now % 86400);
		write_day_row(st, st->last_day);
	}
}

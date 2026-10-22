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

struct hist_base {
	uint64_t up, down;
};

static struct hist_base base_minute;
static struct hist_base base_hour;
static struct hist_base base_day;

/*
 * The counters of the daemon start at zero on every start, the history files
 * keep cumulative values. A restart would therefore write a row that is lower
 * than the previous one, the reader clamps the difference to zero and the
 * whole interval is lost. Continuing from the last row of every file keeps
 * the series continuous across restarts.
 */
static int history_base(const char *dir, const char *file, struct hist_base *out)
{
	char path[256];
	FILE *fp;
	char line[512];
	int found = 0;

	path_join(path, sizeof(path), dir, file);

	fp = fopen(path, "r");
	if (fp == NULL)
		return 0;

	while (fgets(line, sizeof(line), fp) != NULL) {
		unsigned long long up, down;

		if (sscanf(line, "%*s\t%llu\t%llu", &up, &down) == 2) {
			out->up = up;
			out->down = down;
			found = 1;
		}
	}

	fclose(fp);

	return found;
}

int store_init(struct state *st)
{
	ensure_dir(st->cfg.runtime_dir);
	ensure_dir(st->cfg.history_dir);

	history_base(st->cfg.runtime_dir, "minutes.tsv", &base_minute);
	history_base(st->cfg.history_dir, "hours.tsv", &base_hour);
	history_base(st->cfg.history_dir, "days.tsv", &base_day);

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
		size_t len;
		time_t ts;

		if (tab == NULL)
			continue;

		/* fgets keeps the line feed, a second one would create empty rows */
		len = strlen(line);
		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
			line[--len] = '\0';

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
	prune_file(st->cfg.history_dir, "dest_hours.tsv", DEVICE_HOURS_TTL);
	prune_file(st->cfg.history_dir, "dest_days.tsv", long_ttl);
}

struct dest_top {
	uint32_t ip;
	uint64_t up, down, pkts;
};

/*
 * Pick the busiest destinations of the elapsed interval. The baseline of
 * every destination is advanced at the same time, so the next call sees
 * exactly the traffic of the interval that follows.
 */
static int collect_top_destinations(struct state *st, int hourly, struct dest_top *top)
{
	int i, n = 0;

	for (i = 0; i < MAX_DESTINATIONS; i++) {
		struct destination *d = &st->dests[i];
		uint64_t dup, ddown, dpkts, total;

		if (!d->used)
			continue;

		if (hourly) {
			dup = d->up - d->up_prev_h;
			ddown = d->down - d->down_prev_h;
			dpkts = (d->up_pkts - d->up_pkts_prev_h) +
				(d->down_pkts - d->down_pkts_prev_h);
			d->up_prev_h = d->up;
			d->down_prev_h = d->down;
			d->up_pkts_prev_h = d->up_pkts;
			d->down_pkts_prev_h = d->down_pkts;
		} else {
			dup = d->up - d->up_prev_d;
			ddown = d->down - d->down_prev_d;
			dpkts = (d->up_pkts - d->up_pkts_prev_d) +
				(d->down_pkts - d->down_pkts_prev_d);
			d->up_prev_d = d->up;
			d->down_prev_d = d->down;
			d->up_pkts_prev_d = d->up_pkts;
			d->down_pkts_prev_d = d->down_pkts;
		}

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
			int min = 0, j;

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

	return n;
}

static void write_dest_rows(struct state *st, time_t bucket, const char *file, int hourly)
{
	struct dest_top top[TOP_DESTINATIONS];
	char line[320];
	int i, n;

	n = collect_top_destinations(st, hourly, top);

	for (i = 0; i < n; i++) {
		struct dns_entry *e = dns_lookup(st, top[i].ip);
		char ip[24];

		ts_fmt_ip(top[i].ip, ip, sizeof(ip));
		snprintf(line, sizeof(line), "%lld\t%s\t%s\t%llu\t%llu\t%llu",
			 (long long)bucket, ip, (e != NULL) ? e->domain : "",
			 (unsigned long long)top[i].up, (unsigned long long)top[i].down,
			 (unsigned long long)top[i].pkts);
		append_line(st->cfg.history_dir, file, line);
	}
}

static void write_minute_row(struct state *st, time_t bucket)
{
	char line[128];
	uint64_t up, down;

	if (capture_wan_active()) {
		up = st->wan_up + base_minute.up;
		down = st->wan_down + base_minute.down;
	} else {
		up = st->lan_up + base_minute.up;
		down = st->lan_down + base_minute.down;
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

	if (capture_wan_active()) {
		up = st->wan_up + base_hour.up;
		down = st->wan_down + base_hour.down;
	} else {
		up = st->lan_up + base_hour.up;
		down = st->lan_down + base_hour.down;
	}

	snprintf(line, sizeof(line), "%lld\t%llu\t%llu", (long long)bucket,
		 (unsigned long long)up, (unsigned long long)down);
	append_line(st->cfg.history_dir, "hours.tsv", line);

	/* per device hourly deltas */
	for (i = 0; i < MAX_DEVICES; i++) {
		struct device *d = &st->devices[i];
		char mac[18];
		char ip[24];
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

		if (d->ip != 0)
			ts_fmt_ip(d->ip, ip, sizeof(ip));
		else
			ip[0] = '\0';

		snprintf(line, sizeof(line), "%lld\t%s\t%s\t%llu\t%llu", (long long)bucket,
			 mac, ip, (unsigned long long)dup, (unsigned long long)ddown);
		append_line(st->cfg.history_dir, "device_hours.tsv", line);
	}

	/* the busiest destinations of the hour, the day view is too coarse */
	write_dest_rows(st, bucket, "dest_hours.tsv", 1);
}

static void write_day_row(struct state *st, time_t bucket)
{
	char line[320];
	uint64_t up = 0, down = 0;
	int i;

	if (capture_wan_active()) {
		up = st->wan_up + base_day.up;
		down = st->wan_down + base_day.down;
	} else {
		up = st->lan_up + base_day.up;
		down = st->lan_down + base_day.down;
	}

	snprintf(line, sizeof(line), "%lld\t%llu\t%llu", (long long)bucket,
		 (unsigned long long)up, (unsigned long long)down);
	append_line(st->cfg.history_dir, "days.tsv", line);

	for (i = 0; i < MAX_DEVICES; i++) {
		struct device *d = &st->devices[i];
		char mac[18];
		char ip[24];
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

		if (d->ip != 0)
			ts_fmt_ip(d->ip, ip, sizeof(ip));
		else
			ip[0] = '\0';

		snprintf(line, sizeof(line), "%lld\t%s\t%s\t%llu\t%llu", (long long)bucket,
			 mac, ip, (unsigned long long)dup, (unsigned long long)ddown);
		append_line(st->cfg.history_dir, "device_days.tsv", line);
	}

	/* keep the busiest destinations of the day */
	write_dest_rows(st, bucket, "dest_days.tsv", 0);
}

void store_tick(struct state *st)
{
	time_t now = ts_now();
	time_t minute = now - (now % 60);
	time_t hour = now - (now % 3600);
	time_t day = now - (now % 86400);

	/*
	 * The buckets belong to the clock, not to the moment the collector
	 * was started. The running bucket is only marked as done on the
	 * first tick, otherwise a row written one second after the start
	 * would hold almost no traffic and every following row would drift
	 * by the startup offset.
	 */
	if (st->last_minute == 0)
		st->last_minute = minute;

	if (st->last_hour == 0)
		st->last_hour = hour;

	if (st->last_day == 0)
		st->last_day = day;

	if (minute != st->last_minute) {
		st->last_minute = minute;
		write_minute_row(st, minute);
	}

	if (hour != st->last_hour) {
		st->last_hour = hour;
		write_hour_row(st, hour);
	}

	if (day != st->last_day) {
		st->last_day = day;
		write_day_row(st, day);
	}
}

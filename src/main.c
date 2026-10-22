/*
 * luci-app-traffic-stats - daemon main loop
 */

#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ts.h"

static volatile sig_atomic_t keep_running = 1;

static void on_signal(int sig)
{
	(void)sig;
	keep_running = 0;
}

static void read_net_counters(struct state *st)
{
	FILE *fp;
	char line[512];
	unsigned long long rx = 0, tx = 0;
	int found = 0;

	fp = fopen("/proc/net/dev", "r");
	if (fp == NULL)
		return;

	while (fgets(line, sizeof(line), fp) != NULL) {
		char *p, *sp;
		int i;
		unsigned long long v[16];

		p = strchr(line, ':');
		if (p == NULL)
			continue;

		*p = '\0';
		sp = p + 1;

		/* strip the surrounding spaces of the interface name */
		{
			size_t l = strlen(line);
			size_t s = 0;

			while (l > 0 && (line[l - 1] == ' ' || line[l - 1] == '\t'))
				line[--l] = '\0';

			while (line[s] == ' ' || line[s] == '\t')
				s++;

			if (s > 0)
				memmove(line, line + s, strlen(line + s) + 1);
		}

		while (*sp == ' ' || *sp == '\t')
			sp++;

		if (strcmp(line, st->cfg.stats_iface) != 0)
			continue;

		for (i = 0; i < 16; i++) {
			char *end;
			v[i] = strtoull(sp, &end, 10);
			if (end == sp)
				break;
			sp = end;
		}

		if (i >= 9) {
			rx = v[0];
			tx = v[8];
			found = 1;
		}
		break;
	}

	fclose(fp);

	if (!found)
		return;

	st->net_rx_bytes = rx;
	st->net_tx_bytes = tx;
}

static void update_rates(struct state *st, unsigned int dt)
{
	int i;

	if (dt == 0)
		dt = 1;

	for (i = 0; i < MAX_DEVICES; i++) {
		struct device *d = &st->devices[i];

		if (!d->used)
			continue;

		d->up_rate = (d->up - d->up_prev) / dt;
		d->down_rate = (d->down - d->down_prev) / dt;
		d->up_prev = d->up;
		d->down_prev = d->down;
	}

	for (i = 0; i < MAX_DESTINATIONS; i++) {
		struct destination *d = &st->dests[i];

		if (!d->used)
			continue;

		d->up_rate = (d->up - d->up_prev) / dt;
		d->down_rate = (d->down - d->down_prev) / dt;
		d->up_prev = d->up;
		d->down_prev = d->down;
	}

	st->wan_up_rate = (st->wan_up - st->wan_up_prev) / dt;
	st->wan_down_rate = (st->wan_down - st->wan_down_prev) / dt;
	st->wan_up_prev = st->wan_up;
	st->wan_down_prev = st->wan_down;

	st->net_rx_rate = (st->net_rx_bytes - st->net_rx_prev) / dt;
	st->net_tx_rate = (st->net_tx_bytes - st->net_tx_prev) / dt;
	st->net_rx_prev = st->net_rx_bytes;
	st->net_tx_prev = st->net_tx_bytes;
}

int main(int argc, char **argv)
{
	struct state *st;
	struct sigaction sa;
	time_t last_tick = 0;
	char snapshot[256];
	int daemonize = 1;
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--foreground") == 0)
			daemonize = 0;
		else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
			printf("%s\n", TS_VERSION);
			return 0;
		}
	}

	st = calloc(1, sizeof(*st));
	if (st == NULL) {
		fprintf(stderr, "traffic-stats: out of memory\n");
		return 1;
	}

	pthread_mutex_init(&st->lock, NULL);

	if (ts_load_config(st) != 0)
		ts_log("warning: could not read the configuration, using defaults");

	if (daemonize && daemon(0, 0) != 0)
		ts_log("warning: could not daemonize: %s", strerror(errno));

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_signal;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	signal(SIGPIPE, SIG_IGN);

	st->start_time = ts_now();
	st->last_minute = 0;
	st->last_hour = 0;
	st->last_day = 0;

	local_init(st);
	devices_init(st);
	store_init(st);
	store_prune(st);

	if (capture_open(st) == 0) {
		ts_log("no capture interface available, only interface counters are used");
	} else {
		capture_run(st);
	}

	snprintf(snapshot, sizeof(snapshot), "%s/live.json", st->cfg.runtime_dir);

	ts_log("started, lan=[");
	for (i = 0; i < st->cfg.lan_count; i++)
		ts_log("%s%s", (i > 0) ? " " : "", st->cfg.lan_ifaces[i]);
	ts_log("] wan=[");
	for (i = 0; i < st->cfg.wan_count; i++)
		ts_log("%s%s", (i > 0) ? " " : "", st->cfg.wan_ifaces[i]);
	ts_log("]");

	while (keep_running) {
		struct timespec sleep_time;
		struct timespec lock_timeout;
		time_t now;
		unsigned int dt;

		sleep_time.tv_sec = 0;
		sleep_time.tv_nsec = (long)st->cfg.sample_interval * 100000000L;
		nanosleep(&sleep_time, NULL);

		if (!keep_running)
			break;

		now = ts_now();

		if (last_tick == 0)
			dt = (unsigned int)st->cfg.sample_interval;
		else
			dt = (unsigned int)(now - last_tick);

		if (dt == 0)
			dt = 1;

		/*
		 * A timeout keeps the daemon responsive, a collector that
		 * cannot write its snapshots is a broken collector.
		 */
		lock_timeout.tv_sec = 10;
		lock_timeout.tv_nsec = 0;

		if (pthread_mutex_timedlock(&st->lock, &lock_timeout) != 0) {
			ts_log("timeout while writing the snapshot, the collector is overloaded");
			last_tick = now;
			continue;
		}

		read_net_counters(st);
		update_rates(st, dt);
		dns_expire(st);
		local_refresh(st);
		write_snapshot(st, snapshot);
		store_tick(st);
		pthread_mutex_unlock(&st->lock);

		last_tick = now;
	}

	pthread_mutex_lock(&st->lock);
	write_snapshot(st, snapshot);
	pthread_mutex_unlock(&st->lock);

	/*
	 * Remove the state file, a collector that is not running must not
	 * leave a state file behind that looks current.
	 */
	unlink(snapshot);

	capture_close(st);
	pthread_mutex_destroy(&st->lock);
	free(st);

	ts_log("stopped");

	return 0;
}

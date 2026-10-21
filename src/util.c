/*
 * luci-app-traffic-stats - configuration, logging and small helpers
 */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ts.h"

uint32_t ts_now(void)
{
	return (uint32_t)time(NULL);
}

void ts_log(const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "traffic-stats: ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
	fflush(stderr);
}

static void set_str(char *dst, size_t len, const char *src)
{
	if (src == NULL)
		return;
	snprintf(dst, len, "%s", src);
}

static void add_iface(char list[MAX_IFACES][32], int *count, const char *name)
{
	char buf[256];
	char *tok, *save = NULL;
	char *p;

	if (name == NULL || name[0] == '\0')
		return;

	snprintf(buf, sizeof(buf), "%s", name);

	/*
	 * The value may hold several interfaces, either as a list
	 * ("'br-lan' 'lan1'") or separated by spaces or commas.
	 */
	for (p = buf; *p != '\0'; p++)
		if (*p == '\'' || *p == '"' || *p == ',' || *p == ';')
			*p = ' ';

	for (tok = strtok_r(buf, " \t", &save); tok != NULL;
	     tok = strtok_r(NULL, " \t", &save)) {
		int i;

		if (tok[0] == '\0' || *count >= MAX_IFACES)
			continue;

		for (i = 0; i < *count; i++)
			if (strcmp(list[i], tok) == 0)
				break;

		if (i < *count)
			continue;

		snprintf(list[*count], 32, "%s", tok);
		(*count)++;
	}
}

static int parse_bool(const char *v)
{
	if (v == NULL)
		return 0;
	return (strcmp(v, "1") == 0 || strcmp(v, "true") == 0 ||
		strcmp(v, "yes") == 0 || strcmp(v, "on") == 0);
}

/*
 * The configuration is read through the uci command line so that the
 * daemon does not need to link against libuci.
 */
int ts_load_config(struct state *st)
{
	FILE *fp;
	char line[512];
	char key[256];

	/* defaults */
	set_str(st->cfg.stats_iface, sizeof(st->cfg.stats_iface), "pppoe-wan");
	set_str(st->cfg.runtime_dir, sizeof(st->cfg.runtime_dir), "/var/lib/traffic-stats");
	set_str(st->cfg.history_dir, sizeof(st->cfg.history_dir), "/etc/traffic-stats");
	set_str(st->cfg.lease_file, sizeof(st->cfg.lease_file), "/tmp/dhcp.leases");
	st->cfg.sample_interval = 1;
	st->cfg.dns_ttl = 300;
	st->cfg.count_internal = 0;
	st->cfg.history_days = 400;
	st->cfg.verbose = 0;

	fp = popen("uci -q show traffic-stats 2>/dev/null", "r");
	if (fp == NULL)
		return -1;

	while (fgets(line, sizeof(line), fp) != NULL) {
		char *p = strchr(line, '=');
		char *v;

		if (p == NULL)
			continue;
		*p++ = '\0';

		/* strip trailing newline */
		v = strchr(p, '\n');
		if (v != NULL)
			*v = '\0';

		/* strip surrounding quotes */
		if (p[0] == '\'' || p[0] == '"') {
			size_t l = strlen(p);
			if (l >= 2 && p[l - 1] == p[0]) {
				p[l - 1] = '\0';
				p++;
			}
		}

		/* only handle the global section */
		if (strncmp(line, "traffic-stats.global.", 21) != 0)
			continue;

		snprintf(key, sizeof(key), "%.*s", (int)sizeof(key) - 1, line + 21);

		if (strcmp(key, "lan_interfaces") == 0) {
			add_iface(st->cfg.lan_ifaces, &st->cfg.lan_count, p);
		} else if (strcmp(key, "wan_interfaces") == 0) {
			add_iface(st->cfg.wan_ifaces, &st->cfg.wan_count, p);
		} else if (strcmp(key, "stats_iface") == 0) {
			set_str(st->cfg.stats_iface, sizeof(st->cfg.stats_iface), p);
		} else if (strcmp(key, "runtime_dir") == 0) {
			set_str(st->cfg.runtime_dir, sizeof(st->cfg.runtime_dir), p);
		} else if (strcmp(key, "history_dir") == 0) {
			set_str(st->cfg.history_dir, sizeof(st->cfg.history_dir), p);
		} else if (strcmp(key, "lease_file") == 0) {
			set_str(st->cfg.lease_file, sizeof(st->cfg.lease_file), p);
		} else if (strcmp(key, "sample_interval") == 0) {
			st->cfg.sample_interval = atoi(p);
		} else if (strcmp(key, "dns_ttl") == 0) {
			st->cfg.dns_ttl = atoi(p);
		} else if (strcmp(key, "count_internal") == 0) {
			st->cfg.count_internal = parse_bool(p);
		} else if (strcmp(key, "history_days") == 0) {
			st->cfg.history_days = atoi(p);
		} else if (strcmp(key, "verbose") == 0) {
			st->cfg.verbose = parse_bool(p);
		} else if (strcmp(key, "alias") == 0) {
			struct alias *a;

			if (st->cfg.alias_count >= MAX_ALIASES)
				continue;
			a = &st->cfg.aliases[st->cfg.alias_count++];
			if (sscanf(p, "%17[^=]=%63[^\n]", a->mac, a->name) == 2)
				;
			else {
				/* "mac name" separated by whitespace */
				char *sp = strpbrk(p, " \t");
				if (sp == NULL) {
					st->cfg.alias_count--;
					continue;
				}
				*sp = '\0';
				snprintf(a->mac, sizeof(a->mac), "%s", p);
				snprintf(a->name, sizeof(a->name), "%s", sp + 1);
			}
		}
	}

	pclose(fp);

	if (st->cfg.sample_interval < 1)
		st->cfg.sample_interval = 1;
	if (st->cfg.dns_ttl < 10)
		st->cfg.dns_ttl = 10;
	if (st->cfg.history_days < 1)
		st->cfg.history_days = 1;

	return 0;
}

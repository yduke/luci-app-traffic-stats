/*
 * luci-app-traffic-stats - hash tables for devices, destinations and domains
 *
 * All tables are plain open addressed hash tables with linear probing. The
 * daemon runs on a small router, so fixed size tables keep the memory
 * footprint predictable and avoid any allocation in the capture path.
 */

#include <arpa/inet.h>
#include <string.h>

#include "ts.h"

static unsigned int hash_bytes(const uint8_t *p, size_t len, unsigned int seed)
{
	unsigned int h = 2166136261u;
	size_t i;

	for (i = 0; i < len; i++) {
		h ^= p[i];
		h *= 16777619u;
	}
	h ^= seed;
	h *= 16777619u;
	return h;
}

void devices_init(struct state *st)
{
	memset(st->devices, 0, sizeof(st->devices));
	memset(st->dests, 0, sizeof(st->dests));
	memset(st->dns, 0, sizeof(st->dns));
}

/*
 * Devices are identified by their MAC address. The IPv4 address is only
 * kept as a display attribute, a device may well use several of them.
 */
struct device *device_get(struct state *st, const uint8_t *mac, uint32_t ip)
{
	unsigned int idx = hash_bytes(mac, 6, 0) % MAX_DEVICES;
	unsigned int i;

	for (i = 0; i < MAX_DEVICES; i++) {
		struct device *d = &st->devices[(idx + i) % MAX_DEVICES];

		if (!d->used) {
			if (i > MAX_DEVICES / 2)
				break;
			memset(d, 0, sizeof(*d));
			memcpy(d->mac, mac, 6);
			d->ip = ip;
			d->used = 1;
			return d;
		}

		if (memcmp(d->mac, mac, 6) == 0) {
			if (ip != 0)
				d->ip = ip;
			return d;
		}
	}

	return NULL;
}

struct destination *dest_get(struct state *st, uint32_t ip)
{
	unsigned int idx = hash_bytes((const uint8_t *)&ip, sizeof(ip), 7) % MAX_DESTINATIONS;
	unsigned int i;

	for (i = 0; i < MAX_DESTINATIONS; i++) {
		struct destination *d = &st->dests[(idx + i) % MAX_DESTINATIONS];

		if (!d->used) {
			if (i > MAX_DESTINATIONS / 2)
				break;
			memset(d, 0, sizeof(*d));
			d->ip = ip;
			d->used = 1;
			return d;
		}

		if (d->ip == ip)
			return d;
	}

	return NULL;
}

struct dns_entry *dns_lookup(struct state *st, uint32_t ip)
{
	unsigned int idx = hash_bytes((const uint8_t *)&ip, sizeof(ip), 13) % MAX_DNS_ENTRIES;
	unsigned int i;

	for (i = 0; i < MAX_DNS_ENTRIES; i++) {
		struct dns_entry *e = &st->dns[(idx + i) % MAX_DNS_ENTRIES];

		if (!e->used)
			return NULL;

		if (e->ip == ip)
			return (e->expiry > ts_now()) ? e : NULL;
	}

	return NULL;
}

void dns_learn(struct state *st, uint32_t ip, const char *domain, time_t expiry)
{
	unsigned int idx;
	unsigned int i;
	struct dns_entry *e;

	if (ip == 0 || domain == NULL || domain[0] == '\0')
		return;

	idx = hash_bytes((const uint8_t *)&ip, sizeof(ip), 13) % MAX_DNS_ENTRIES;

	for (i = 0; i < MAX_DNS_ENTRIES; i++) {
		e = &st->dns[(idx + i) % MAX_DNS_ENTRIES];

		if (e->used && e->ip == ip) {
			snprintf(e->domain, sizeof(e->domain), "%s", domain);
			e->expiry = expiry;
			return;
		}

		if (!e->used) {
			memset(e, 0, sizeof(*e));
			e->ip = ip;
			snprintf(e->domain, sizeof(e->domain), "%s", domain);
			e->expiry = expiry;
			e->used = 1;
			return;
		}
	}

	/* table full, replace the entry closest to expiry */
	e = &st->dns[idx];
	for (i = 1; i < MAX_DNS_ENTRIES; i++) {
		struct dns_entry *c = &st->dns[(idx + i) % MAX_DNS_ENTRIES];
		if (c->expiry < e->expiry)
			e = c;
	}
	e->ip = ip;
	snprintf(e->domain, sizeof(e->domain), "%s", domain);
	e->expiry = expiry;
	e->used = 1;
}

void dns_expire(struct state *st)
{
	time_t now = ts_now();
	unsigned int i;

	for (i = 0; i < MAX_DNS_ENTRIES; i++) {
		if (st->dns[i].used && st->dns[i].expiry <= now)
			st->dns[i].used = 0;
	}
}

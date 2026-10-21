/*
 * luci-app-traffic-stats - local network detection and device naming
 */

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ts.h"

#define MAX_PREFIXES 32
#define MAX_LEASES    512
#define MAX_HOSTS     512

/* the local IPv4 prefixes, used to tell internal from external traffic */
static struct prefix4 prefixes[MAX_PREFIXES];
static int prefix_count;

/* the local IPv6 prefixes */
static struct prefix6 prefixes6[MAX_PREFIXES];
static int prefix6_count;

/* static DHCP entries: MAC -> host name */
struct host {
	char mac[18];
	char name[64];
};
static struct host hosts[MAX_HOSTS];
static int host_count;
static time_t host_mtime;

/* the hardware addresses of the local interfaces */
struct localmac {
	uint8_t mac[6];
};
static struct localmac localmacs[MAX_PREFIXES];
static int localmac_count;

/* lease cache: MAC -> host name */
struct lease {
	char mac[18];
	char name[64];
};
static struct lease leases[MAX_LEASES];
static int lease_count;
static time_t lease_mtime;

int local_init(struct state *st)
{
	struct ifaddrs *ifa, *p;
	int n = 0;
	int n6 = 0;
	int sock;

	(void)st;

	prefix_count = 0;

	if (getifaddrs(&ifa) != 0)
		return -1;

	sock = socket(AF_INET, SOCK_DGRAM, 0);

	for (p = ifa; p != NULL && n < MAX_PREFIXES; p = p->ifa_next) {
		struct sockaddr_in *sin;
		uint32_t mask = 0xffffffffu;

		/* remember the hardware address of the local interfaces */
		if (sock >= 0 && localmac_count < MAX_PREFIXES) {
			struct ifreq ifr;

			memset(&ifr, 0, sizeof(ifr));
			snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", p->ifa_name);

			if (ioctl(sock, SIOCGIFHWADDR, &ifr) == 0) {
				memcpy(localmacs[localmac_count].mac, ifr.ifr_hwaddr.sa_data, 6);
				localmac_count++;
			}
		}

		if (p->ifa_addr == NULL)
			continue;

		if (p->ifa_addr->sa_family == AF_INET6) {
			struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)p->ifa_addr;
			int bits = 64;

			if (n6 >= MAX_PREFIXES)
				continue;
			if (!(p->ifa_flags & IFF_UP))
				continue;

			if (sock >= 0) {
				struct ifreq ifr;

				memset(&ifr, 0, sizeof(ifr));
				snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", p->ifa_name);

				if (ioctl(sock, SIOCGIFNETMASK, &ifr) == 0) {
					struct sockaddr_in6 *m6 = (struct sockaddr_in6 *)&ifr.ifr_netmask;
					int b;

					for (b = 0; b < 16; b++) {
						uint8_t v = ((const uint8_t *)&m6->sin6_addr)[b];
						int ones = 0;
						int k;

						if (v == 0xff) {
							bits = (b + 1) * 8;
							continue;
						}

						for (k = 7; k >= 0; k--) {
							if (v & (1 << k))
								ones++;
							else
								break;
						}

						bits = b * 8 + ones;
						break;
					}
				}
			}

			memcpy(prefixes6[n6].addr, &sin6->sin6_addr, 16);
			prefixes6[n6].bits = (bits > 0) ? bits : 128;
			n6++;
			continue;
		}

		if (p->ifa_addr->sa_family != AF_INET)
			continue;
		if (!(p->ifa_flags & IFF_UP))
			continue;

		sin = (struct sockaddr_in *)p->ifa_addr;

		if (sock >= 0) {
			struct ifreq ifr;

			memset(&ifr, 0, sizeof(ifr));
			snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", p->ifa_name);

			if (ioctl(sock, SIOCGIFNETMASK, &ifr) == 0)
				mask = ((struct sockaddr_in *)&ifr.ifr_netmask)->sin_addr.s_addr;
		}

		prefixes[n].addr = sin->sin_addr.s_addr;
		prefixes[n].mask = mask;
		n++;
	}

	if (sock >= 0)
		close(sock);

	freeifaddrs(ifa);
	prefix_count = n;
	prefix6_count = n6;

	if (st->cfg.verbose) {
		int i;

		for (i = 0; i < prefix_count; i++)
			ts_log("local prefix addr=%08x mask=%08x",
			       (unsigned int)prefixes[i].addr,
			       (unsigned int)prefixes[i].mask);

		for (i = 0; i < prefix6_count; i++)
			ts_log("local prefix6 bits=%d", prefixes6[i].bits);
	}

	return 0;
}

/*
 * Check whether an IPv6 address belongs to one of the local prefixes.
 */
int is_local6(struct state *st, const uint8_t *addr)
{
	int i;

	(void)st;

	for (i = 0; i < prefix6_count; i++) {
		int bits = prefixes6[i].bits;
		int matched = 1;
		int b;

		for (b = 0; b < 16; b++) {
			int left = bits - b * 8;
			uint8_t mask;

			if (left <= 0)
				break;

			if (left >= 8)
				mask = 0xff;
			else
				mask = (uint8_t)(0xff << (8 - left));

			if ((addr[b] & mask) != (prefixes6[i].addr[b] & mask)) {
				matched = 0;
				break;
			}
		}

		if (matched)
			return 1;
	}

	return 0;
}

int is_local4(struct state *st, uint32_t ip)
{
	int i;

	(void)st;

	for (i = 0; i < prefix_count; i++) {
		if ((ip & prefixes[i].mask) == (prefixes[i].addr & prefixes[i].mask))
			return 1;
	}

	return 0;
}

/*
 * Reload the DHCP lease file when it changed. The file is small, so a full
 * re-read is cheaper and far more robust than tracking offsets.
 */
static void leases_reload(const char *path)
{
	struct stat sb;
	FILE *fp;
	char line[256];

	if (stat(path, &sb) != 0) {
		lease_count = 0;
		return;
	}

	if (lease_mtime == sb.st_mtime && lease_count > 0)
		return;

	lease_mtime = sb.st_mtime;
	lease_count = 0;

	fp = fopen(path, "r");
	if (fp == NULL) {
		lease_count = 0;
		return;
	}

	while (fgets(line, sizeof(line), fp) != NULL) {
		unsigned long expiry;
		char mac[18], ip[40], host[64], cid[80];
		int n;

		n = sscanf(line, "%lu %17s %39s %63s %79s",
			   &expiry, mac, ip, host, cid);
		if (n < 4)
			continue;

		if (host[0] == '*' || host[0] == '\0')
			continue;

		if (lease_count >= MAX_LEASES)
			break;

		snprintf(leases[lease_count].mac, sizeof(leases[lease_count].mac), "%s", mac);
		snprintf(leases[lease_count].name, sizeof(leases[lease_count].name), "%s", host);
		lease_count++;
	}

	fclose(fp);
}

/*
 * Read the static DHCP entries of /etc/config/dhcp. Every device that was
 * given a name by the user is listed there.
 */
static void hosts_reload(const char *path)
{
	struct stat sb;
	FILE *fp;
	char line[512];
	char mac[18], name[64];
	int in_host = 0;

	if (stat(path, &sb) != 0) {
		host_count = 0;
		return;
	}

	if (host_mtime == sb.st_mtime && host_count > 0)
		return;

	host_mtime = sb.st_mtime;
	host_count = 0;

	fp = fopen(path, "r");
	if (fp == NULL) {
		host_count = 0;
		return;
	}

	mac[0] = '\0';
	name[0] = '\0';

	while (fgets(line, sizeof(line), fp) != NULL) {
		char kw[32], key[64], vbuf[192];
		char *p = line;
		char *val;

		while (*p == ' ' || *p == '\t')
			p++;

		if (*p == '\n' || *p == '\0' || *p == '#')
			continue;

		if (strncmp(p, "config ", 7) == 0) {
			/* a new section starts, store the previous one */
			if (in_host && mac[0] != '\0' && name[0] != '\0' && host_count < MAX_HOSTS) {
				snprintf(hosts[host_count].mac, sizeof(hosts[host_count].mac), "%s", mac);
				snprintf(hosts[host_count].name, sizeof(hosts[host_count].name), "%s", name);
				host_count++;
			}

			in_host = 0;
			mac[0] = '\0';
			name[0] = '\0';

			if (sscanf(p, "config %31s", kw) == 1 && strcmp(kw, "host") == 0)
				in_host = 1;

			continue;
		}

		if (!in_host)
			continue;

		if (sscanf(p, "%31s %63s %191[^\n]", kw, key, vbuf) != 3)
			continue;

		if (strcmp(kw, "option") != 0 && strcmp(kw, "list") != 0)
			continue;

		/* strip the quotes of the value */
		{
			size_t l = strlen(vbuf);

			while (l > 0 && (vbuf[l - 1] == '\n' || vbuf[l - 1] == '\r' || vbuf[l - 1] == ' '))
				vbuf[--l] = '\0';

			if (l >= 2 && (vbuf[0] == '\'' || vbuf[0] == '"') && vbuf[l - 1] == vbuf[0]) {
				vbuf[l - 1] = '\0';
				val = vbuf + 1;
			} else {
				val = vbuf;
			}
		}

		if (strcmp(key, "mac") == 0)
			snprintf(mac, sizeof(mac), "%.*s", (int)sizeof(mac) - 1, val);
		else if (strcmp(key, "name") == 0 || strcmp(key, "host") == 0)
			snprintf(name, sizeof(name), "%.*s", (int)sizeof(name) - 1, val);
	}

	if (in_host && mac[0] != '\0' && name[0] != '\0' && host_count < MAX_HOSTS) {
		snprintf(hosts[host_count].mac, sizeof(hosts[host_count].mac), "%s", mac);
		snprintf(hosts[host_count].name, sizeof(hosts[host_count].name), "%s", name);
		host_count++;
	}

	fclose(fp);
}

const char *device_name(struct state *st, const uint8_t *mac, uint32_t ip, char *buf, size_t len)
{
	char macstr[18];
	int i;

	snprintf(macstr, sizeof(macstr), "%02x:%02x:%02x:%02x:%02x:%02x",
		 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	/* the traffic of the router itself */
	for (i = 0; i < localmac_count; i++) {
		if (memcmp(localmacs[i].mac, mac, 6) == 0) {
			snprintf(buf, len, "%s", "OpenWRT");
			return buf;
		}
	}

	/* the names of the static DHCP entries have the highest priority */
	hosts_reload("/etc/config/dhcp");

	for (i = 0; i < host_count; i++) {
		if (strcasecmp(hosts[i].mac, macstr) == 0) {
			snprintf(buf, len, "%s", hosts[i].name);
			return buf;
		}
	}

	/* user defined aliases win over the names of the DHCP server */
	for (i = 0; i < st->cfg.alias_count; i++) {
		if (strcasecmp(st->cfg.aliases[i].mac, macstr) == 0) {
			snprintf(buf, len, "%s", st->cfg.aliases[i].name);
			return buf;
		}
	}

	leases_reload(st->cfg.lease_file);

	for (i = 0; i < lease_count; i++) {
		if (strcasecmp(leases[i].mac, macstr) == 0) {
			snprintf(buf, len, "%s", leases[i].name);
			return buf;
		}
	}

	/* fall back to the address so the entry is still identifiable */
	if (ip != 0) {
		struct in_addr a;
		a.s_addr = ip;
		snprintf(buf, len, "%s", inet_ntoa(a));
		return buf;
	}

	snprintf(buf, len, "%s", macstr);
	return buf;
}

void local_refresh(struct state *st)
{
	leases_reload(st->cfg.lease_file);
	hosts_reload("/etc/config/dhcp");
}

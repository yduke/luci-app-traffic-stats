/*
 * luci-app-traffic-stats - DNS message parser
 *
 * The daemon learns which name belongs to which address by looking at the
 * DNS traffic that crosses the router. This is completely passive, the
 * resolver configuration is not modified and no additional DNS request is
 * sent to the network.
 */

#include <arpa/inet.h>
#include <string.h>

#include "ts.h"

#define DNS_TYPE_A    1
#define DNS_TYPE_AAAA 28

struct dns_ctx {
	const uint8_t *msg;
	size_t         len;
};

/*
 * Read a (possibly compressed) domain name.
 *
 * "end" receives the offset right behind the name inside the original
 * message, which is what the record parser needs to continue.
 */
static int dns_read_name(struct dns_ctx *ctx, size_t pos, char *out, size_t outlen,
			 size_t *end)
{
	size_t p = pos;
	size_t after = 0;
	int jumped = 0;
	size_t o = 0;

	if (outlen == 0)
		return -1;

	out[0] = '\0';

	for (;;) {
		uint8_t l;

		if (p >= ctx->len)
			return -1;

		l = ctx->msg[p];

		if ((l & 0xc0) == 0xc0) {
			size_t target;

			if (p + 1 >= ctx->len)
				return -1;

			target = (size_t)(((l & 0x3f) << 8) | ctx->msg[p + 1]);

			if (!jumped) {
				after = p + 2;
				jumped = 1;
			}

			if (target >= p)
				return -1;  /* only backward pointers are valid */

			p = target;
			continue;
		}

		if (l == 0) {
			p++;
			break;
		}

		if (p + 1 + l > ctx->len)
			return -1;

		if (o > 0 && o + 1 < outlen)
			out[o++] = '.';

		if (o + l < outlen) {
			memcpy(out + o, ctx->msg + p + 1, l);
			o += l;
		}

		p += 1 + l;
	}

	out[o < outlen ? o : outlen - 1] = '\0';

	if (end != NULL)
		*end = jumped ? after : p;

	return 0;
}

/*
 * Parse one DNS message. When it is a reply the A records are mapped back
 * to the name that was asked for, so the user sees the domain that was
 * actually visited instead of a CDN alias.
 */
int dns_parse_packet(struct state *st, const uint8_t *payload, size_t len, int is_tcp,
		     int from_client)
{
	struct dns_ctx ctx;
	size_t pos;
	unsigned int qdcount, ancount;
	char qname[256];
	int i;

	if (is_tcp) {
		if (len < 2)
			return -1;
		pos = 2;
	} else {
		pos = 0;
	}

	if (len < pos + 12)
		return -1;

	ctx.msg = payload;
	ctx.len = len;

	qdcount = (unsigned int)((payload[pos + 4] << 8) | payload[pos + 5]);
	ancount = (unsigned int)((payload[pos + 6] << 8) | payload[pos + 7]);
	pos += 12;

	qname[0] = '\0';

	for (i = 0; i < (int)qdcount; i++) {
		size_t end = 0;

		if (pos >= len)
			break;

		if (dns_read_name(&ctx, pos, qname, sizeof(qname), &end) != 0)
			break;

		pos = end;

		if (pos + 4 > len)
			break;

		pos += 4;   /* QTYPE + QCLASS */
	}

	if (from_client) {
		st->dns_queries++;
		return 0;
	}

	st->dns_responses++;

	for (i = 0; i < (int)ancount; i++) {
		char rname[256];
		size_t end = 0;
		unsigned int type, ttl, rdlen;
		uint32_t ip;

		if (pos >= len)
			break;

		if (dns_read_name(&ctx, pos, rname, sizeof(rname), &end) != 0)
			break;

		pos = end;

		if (pos + 10 > len)
			break;

		type = (unsigned int)((payload[pos] << 8) | payload[pos + 1]);
		ttl = ((uint32_t)payload[pos + 4] << 24) | ((uint32_t)payload[pos + 5] << 16) |
		      ((uint32_t)payload[pos + 6] << 8) | (uint32_t)payload[pos + 7];
		rdlen = (unsigned int)((payload[pos + 8] << 8) | payload[pos + 9]);
		pos += 10;

		if (pos + rdlen > len)
			break;

		if (type == DNS_TYPE_A && rdlen == 4) {
			const char *name = (qname[0] != '\0') ? qname : rname;

			ip = (uint32_t)payload[pos] | ((uint32_t)payload[pos + 1] << 8) |
			     ((uint32_t)payload[pos + 2] << 16) | ((uint32_t)payload[pos + 3] << 24);

			/*
			 * The TTL of the record says how long a resolver may
			 * cache the answer, it does not say how long the name
			 * is useful for the statistics. A short record TTL
			 * would drop almost every name within minutes and the
			 * traffic could not be labelled any more. The name is
			 * kept for the configured lifetime, a new answer for
			 * the same address replaces it immediately.
			 */
			(void)ttl;
			dns_learn(st, ip, name, ts_now() + (time_t)st->cfg.dns_ttl);
		}

		pos += rdlen;
	}

	return 0;
}

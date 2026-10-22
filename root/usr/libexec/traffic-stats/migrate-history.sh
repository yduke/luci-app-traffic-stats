#!/bin/sh
#
# Convert the address columns of the history files written by older
# versions. They stored the IPv4 address as a decimal number, for example
# 3506612416 instead of 192.168.2.209, which the interface cannot show.
#
# The conversion is idempotent, an address that already contains dots is
# left alone.
#

DIR="${1:-/etc/traffic-stats}"

convert() {
	local file="$1"
	local col="$2"

	[ -f "$file" ] || return 0

	awk -v col="$col" '
	# the address is stored little endian, the lowest byte is the first octet
	function dotted(n) {
		return n % 256 "." int(n / 256) % 256 "." int(n / 65536) % 256 "." int(n / 16777216) % 256
	}

	BEGIN { FS = OFS = "\t" }

	NF == 0 { next }

	NF >= col && $col ~ /^[0-9]+$/ { $col = dotted($col + 0) }

	{ print }
	' "$file" > "$file.migrate" && mv "$file.migrate" "$file"
}

# dest_hours.tsv:    ts, address, domain, up, down, packets
convert "$DIR/dest_hours.tsv" 2

# dest_days.tsv:     ts, address, domain, up, down, packets
convert "$DIR/dest_days.tsv" 2

# device_hours.tsv:  ts, mac, address, up, down
convert "$DIR/device_hours.tsv" 3

# device_days.tsv:   ts, mac, address, up, down
convert "$DIR/device_days.tsv" 3

exit 0

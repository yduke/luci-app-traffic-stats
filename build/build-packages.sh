#!/bin/sh
#
# Build the luci-app-traffic-stats packages with an OpenWrt SDK.
#
# The script produces both package formats that OpenWrt supports:
#
#   luci-app-traffic-stats_<version>-r<rev>_aarch64_cortex-a53.ipk   (opkg)
#   luci-app-traffic-stats_<version>-r<rev>_aarch64_cortex-a53.apk   (apk)
#
# The ipk package is built with the package manager of the SDK switched to
# opkg, the apk package with the default apk configuration. Both are
# produced from the very same source tree.
#
# Usage:
#   build/build-packages.sh [sdk-directory]
#
# The default SDK directory is /root/sdk/openwrt-sdk-25.12.6-qualcommax-ipq60xx
#

set -e

PACKAGE=luci-app-traffic-stats
PACKAGE_NAME=traffic-stats
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
OUT_DIR="$SRC_DIR/dist"

SDK_DIR="${1:-${SDK_DIR:-/root/sdk/openwrt-sdk-25.12.6-qualcommax-ipq60xx_gcc-14.3.0_musl.Linux-x86_64}}"

if [ ! -f "$SDK_DIR/.config" ] && [ ! -f "$SDK_DIR/rules.mk" ]; then
	echo "error: $SDK_DIR is not an OpenWrt SDK" >&2
	exit 1
fi

echo "==> using SDK $SDK_DIR"

rm -f "$OUT_DIR"/*.ipk "$OUT_DIR"/*.apk
mkdir -p "$OUT_DIR"

# copy the package into the build tree
rm -rf "$SDK_DIR/package/$PACKAGE"
mkdir -p "$SDK_DIR/package/$PACKAGE"

for item in Makefile LICENSE src root htdocs i18n; do
	if [ -e "$SRC_DIR/$item" ]; then
		cp -a "$SRC_DIR/$item" "$SDK_DIR/package/$PACKAGE/"
	fi
done

# the revision of the package is taken from the git history
if [ -d "$SRC_DIR/.git" ]; then
	cp -a "$SRC_DIR/.git" "$SDK_DIR/package/$PACKAGE/"
fi

# make sure the build system notices the changed sources
find "$SDK_DIR/package/$PACKAGE" -type f -not -path "*/.git/*" -exec touch {} +

cd "$SDK_DIR"

# make sure the configuration exists
if [ ! -f .config ]; then
	make defconfig
fi

build_package() {
	format="$1"

	echo "==> building $format package"

	if [ "$format" = "apk" ]; then
		enable_apk y
	else
		enable_apk n
	fi

	make defconfig >/dev/null

	rm -rf tmp/info package/feeds/*/metadata 2>/dev/null || true

	if ! make "package/$PACKAGE/compile" V=s; then
		echo "error: the $format package could not be built" >&2
		exit 1
	fi

	mkdir -p "$OUT_DIR"

	find bin -type f \( -name "$PACKAGE*.ipk" -o -name "$PACKAGE*.apk" \
		-o -name "luci-i18n-$PACKAGE_NAME*.ipk" -o -name "luci-i18n-$PACKAGE_NAME*.apk" \) \
		-exec cp -f {} "$OUT_DIR/" \;
}

# The SDK decides with CONFIG_USE_APK whether the packages are built for
# opkg (ipk) or for apk. The default of the SDK is patched as well, so a
# "make defconfig" does not switch the format back.
enable_apk() {
	value="$1"

	if [ ! -f Config-build.in.orig ]; then
		cp Config-build.in Config-build.in.orig
	fi

	python3 - "$value" <<-'EOF'
		import re, sys
		value = sys.argv[1]
		path = 'Config-build.in'
		text = open(path).read()
		text = re.sub(r'(config USE_APK\n\tbool\n\tdefault )[yn]\n',
		              r'\g<1>' + value + '\n', text)
		open(path, 'w').write(text)
	EOF

	if [ "$value" = "y" ]; then
		sed -i 's/^# CONFIG_USE_APK is not set/CONFIG_USE_APK=y/' .config
	else
		sed -i 's/^CONFIG_USE_APK=y/# CONFIG_USE_APK is not set/' .config
	fi
}

build_package ipk
build_package apk

echo
echo "==> packages in $OUT_DIR"
ls -la "$OUT_DIR"

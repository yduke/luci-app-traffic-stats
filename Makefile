#
# luci-app-traffic-stats - traffic monitoring and statistics for LuCI
#
# This is free software, licensed under the Apache License, Version 2.0.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-traffic-stats
PKG_VERSION:=1.0.2
PKG_RELEASE:=1

PKG_LICENSE:=GPL-2.0-only
PKG_LICENSE_FILES:=LICENSE
PKG_MAINTAINER:=luci-app-traffic-stats contributors

LUCI_TITLE:=Traffic statistics and monitoring
LUCI_DESCRIPTION:=Real time traffic monitoring and statistics for the local \
	network and the internet connection. Reports the bandwidth used by \
	every device of the local network, the most visited destinations and \
	the traffic volume of the last hours, days, months and years.
LUCI_DEPENDS:=+luci-base +rpcd +rpcd-mod-ucode +ucode +ucode-mod-fs \
	+ucode-mod-uci +libc

# the package ships a native binary, so it is architecture specific
LUCI_PKGARCH:=

# keep the shipped sources readable, the browser cache handles the rest
LUCI_MINIFY_JS:=0
LUCI_MINIFY_CSS:=0

LUCI_URL:=https://github.com/openwrt/luci
LUCI_MAINTAINER:=luci-app-traffic-stats contributors

# the Chinese translation is part of this package, the separate
# translation package of the LuCI build system is not used
PKG_BUILD_DEPENDS:=luci-base

define Package/$(PKG_NAME)/conffiles
/etc/config/traffic-stats
endef

define Package/$(PKG_NAME)/postinst
#!/bin/sh

[ -n "$${IPKG_INSTROOT}" ] || {
	chmod 0755 /etc/init.d/traffic-stats
	chmod 0755 /etc/uci-defaults/40_traffic-stats 2>/dev/null
	mkdir -p /var/lib/traffic-stats
	mkdir -p /etc/traffic-stats
	# older versions stored the addresses as decimal numbers
	sh /usr/libexec/traffic-stats/migrate-history.sh /etc/traffic-stats
	/etc/init.d/traffic-stats enable
	/etc/init.d/traffic-stats restart
	/etc/init.d/rpcd reload
	rm -f /tmp/luci-indexcache.*
	rm -rf /tmp/luci-modulecache/
}

exit 0
endef

# the translations are not shipped as a separate package, they are
# compiled into the application package instead
define Build/Prepare/luci-app-traffic-stats
	$(CP) ./i18n $(PKG_BUILD_DIR)/
endef

include $(TOPDIR)/feeds/luci/luci.mk

# call BuildPackage - OpenWrt buildroot signature

# luci-app-traffic-stats

Traffic monitoring and statistics for LuCI on OpenWrt.

The application shows the realtime traffic of the local network and of the
internet connection, the devices with the highest traffic volume, the most
used destinations of the internet and the traffic history of the last hours,
days, months and years.

![status](https://img.shields.io/badge/luci-app--traffic--stats-in%20status-blue)

## Features

* **Realtime traffic** of the internet connection, updated every two seconds
  with a live chart.
* **Devices of the local network** ranked by their upload and download
  volume, with the MAC address, the IP address and the name from the DHCP
  lease of the device. The router itself is part of the list.
* **Destinations on the internet**, the most visited domains and IP
  addresses as well as the addresses with the highest traffic volume. The
  domain names are learned from the DNS traffic of the router.
* **Traffic history** with the ranges *24 hours*, *7 days*, *30 days* and
  *1 year*.
* **Interface for the LuCI theme**, follows the light and the dark mode of
  the Argon theme.
* **Translations**, the interface follows the language of the LuCI, English
  and Simplified Chinese are shipped.

## How the traffic is measured

The collector reads a copy of the traffic that crosses the router with a
read only packet socket (`AF_PACKET`). It never changes a firewall rule, a
route, a DNS setting or an interface, installing the application therefore
cannot affect the connectivity of the router.

Reading the traffic on the LAN side gives the traffic of every device, the
MAC address of the frame identifies the device. Reading the traffic on the
WAN side gives the complete volume of the internet connection, including
the traffic that the router itself generates.

The statistics are exact, unlike the values of the connection tracking
table, they are not affected by the hardware offloading of the firewall.

## Installation

Download the package that matches the package manager of the router:

| Router | Package |
| ------ | ------- |
| OpenWrt 19.07 - 24.10, Kwrt | `luci-app-traffic-stats_1.0.0-r1_aarch64_cortex-a53.ipk` |
| OpenWrt 25.12 and newer | `luci-app-traffic-stats-1.0.0-r1.apk` |

```sh
# with opkg
opkg install luci-app-traffic-stats_1.0.0-r1_aarch64_cortex-a53.ipk

# with apk
apk add luci-app-traffic-stats-1.0.0-r1.apk
```

The Chinese translation is a separate package:

```sh
opkg install luci-i18n-traffic-stats-zh-cn_0.261021.27580_all.ipk
apk add luci-i18n-traffic-stats-zh-cn-0.261021.27580.apk
```

After the installation the application is available at *Status -> Traffic
Statistics*.

## Configuration

The settings are stored in `/etc/config/traffic-stats`:

| Option | Description |
| ------ | ----------- |
| `lan_interfaces` | Interfaces that face the local network, the traffic of every device is attributed here. |
| `wan_interfaces` | Interfaces that face the internet, they provide the total traffic volume and the destination statistics. |
| `stats_iface` | Interface whose byte counters are shown as the total traffic of the internet connection. |
| `sample_interval` | Length of a sampling interval in seconds. |
| `dns_ttl` | Number of seconds a learned domain name is kept. |
| `count_internal` | Also count the traffic between the devices of the local network. |
| `history_days` | Number of days the daily statistics are kept on the flash memory. |
| `alias` | Optional friendly name for a device, in the form `mac address=name`. |

The interfaces are detected automatically during the installation, the
values can be corrected in the settings of the application.

## Building

The package is a regular OpenWrt package, it can be built with the SDK of
the target:

```sh
build/build-packages.sh [sdk-directory]
```

The script builds the package twice, once for `opkg` (`.ipk`) and once for
`apk` (`.apk`), the results are placed in `dist/`.

The default SDK directory is
`/root/sdk/openwrt-sdk-25.12.6-qualcommax-ipq60xx_gcc-14.3.0_musl.Linux-x86_64`.

Requirements of the build host: `make`, `gcc`, `gettext`, `python3` and the
usual OpenWrt build dependencies.

### Translations

The translation template is generated with the script of the LuCI
repository:

```sh
perl <luci>/build/i18n-scan.pl . > po/templates/traffic-stats.pot
```

The Chinese translation is created with:

```sh
python3 build/make-po.py po/templates/traffic-stats.pot po/zh_Hans/traffic-stats.po zh_Hans
```

### Tests

The LuCI view is checked with a smoke test that renders it with mocked
LuCI modules:

```sh
node build/view-smoke-test.js
```

## Layout of the sources

```
src/                     the collector daemon
  main.c                 main loop, interface counters
  capture.c              packet capture and protocol decoding
  ipflow.c               (reserved)
  dns.c                  DNS message parser
  hash.c                 hash tables for devices, destinations and domains
  local.c                local networks and device names
  store.c                history files
  json.c                 runtime snapshot
  util.c                 configuration
root/                    files that are installed on the router
  etc/config/            configuration
  etc/init.d/            procd service
  etc/uci-defaults/      detection of the interfaces
  usr/libexec/rpcd/      the ubus API of the application
  usr/share/rpcd/acl.d/  access control
  usr/share/luci/menu.d/ menu entry
htdocs/                  the LuCI application
  luci-static/resources/view/traffic-stats/main.js
  luci-static/resources/traffic-stats/traffic-stats.css
po/                      translations
build/                   build and test scripts
```

## License

MIT, see [LICENSE](LICENSE).

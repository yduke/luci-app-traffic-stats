# luci-app-traffic-stats

Traffic monitoring and statistics for LuCI on OpenWrt.

The application shows the realtime traffic of the local network and of the
internet connection, the devices with the highest traffic volume, the most
used destinations of the internet and the traffic history of the last hours,
days, months and years.

![license](https://img.shields.io/badge/license-MIT-blue)
![luci](https://img.shields.io/badge/LuCI-OpenWrt%20ready-blue)
![lang](https://img.shields.io/badge/language-English%20%2F%20%E7%AE%80%E4%BD%93%E4%B8%AD%E6%96%87-blue)

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

The build produces two files, they belong together — the application
including the Chinese translation, in the format of the package manager of
the router:

| File | Package manager | Content |
| ---- | --------------- | ------- |
| `luci-app-traffic-stats_1.0.0-r1_aarch64_cortex-a53.ipk` | opkg | the application with the Chinese translation |
| `luci-app-traffic-stats-1.0.0-r1.apk` | apk | the application with the Chinese translation |

The `.ipk` and the `.apk` contain exactly the same files, they only differ
in the container format. Install the one that matches the router.

```sh
# opkg based routers (OpenWrt up to 25.12 with opkg, most vendor builds)
opkg install luci-app-traffic-stats_1.0.0-r1_aarch64_cortex-a53.ipk

# apk based routers (OpenWrt 25.12 and newer)
apk add luci-app-traffic-stats-1.0.0-r1.apk
```

### Language

The application has no language setting of its own, it follows the
language of the LuCI interface:

* the strings of the interface are written in English,
* the Chinese translation is compiled into the package, LuCI loads it when
  the interface language is *简体中文*,
* with any other interface language the English strings are shown.

The language is changed in *System -> System -> Language* and takes effect
after a reload of the page.

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

The translations live in `i18n/`, they are compiled into the application
package and are not shipped as a separate translation package like the
other LuCI applications do.

The translation template is generated with the script of the LuCI
repository:

```sh
perl <luci>/build/i18n-scan.pl . > i18n/templates/traffic-stats.pot
```

The Chinese translation is created with:

```sh
python3 build/make-po.py i18n/templates/traffic-stats.pot i18n/zh_Hans/traffic-stats.po zh_Hans
```

### Tests

The LuCI view is checked with a smoke test that renders it with mocked
LuCI modules:

```sh
node build/view-smoke-test.js
```

The test renders the view, runs the polling callback, checks the status
bar of the page and verifies that the charts contain their series. The
output of a real router can be used as input:

```sh
ssh root@192.168.2.1 'ubus call luci.trafficstats overview' > overview.json
node build/view-smoke-test.js overview.json
```

To look at the page in a browser without a login, a small proxy can be
used that adds the session cookie of the router to every request:

```sh
node build/session-proxy.js <session-id> 8099
chromium --headless=new --dump-dom http://127.0.0.1:8099/cgi-bin/luci/admin/status/traffic-stats
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
i18n/                    translations, compiled into the package
build/                   build and test scripts
  build-packages.sh       builds the ipk and the apk package
  make-po.py              creates the translated po file
  view-smoke-test.js      renders the view and checks the result
  session-proxy.js       adds the session cookie for a browser test
```

## License

MIT, see [LICENSE](LICENSE).

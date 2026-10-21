#!/usr/bin/env python3
"""Create a translated .po file from the .pot template of this package."""

import re
import sys

TRANSLATIONS = {
    "1 year": "一年内",
    "24 hours": "24 小时内",
    "30 days": "一个月内",
    "7 days": "一星期内",
    "Address": "地址",
    "Addresses": "地址数",
    "Also account for the traffic between the devices of the local network.":
        "同时统计内网设备之间的流量。",
    "Collected packets": "已收集数据包",
    "Collector is not running": "采集器未运行",
    "Collector running": "采集器运行中",
    "Comma separated list of the interfaces that face the internet. They provide "
    "the total traffic volume and the destination statistics.":
        "以逗号分隔的外网接口列表。该接口提供总流量和目标地址统计。",
    "Comma separated list of the interfaces that face the local network. The "
    "traffic of every device is attributed here.":
        "以逗号分隔的内网接口列表。所有内网设备的流量都在此统计。",
    "Count internal traffic": "统计内网流量",
    "Destinations": "外网目标",
    "Destinations on the internet": "外网访问目标",
    "Device": "设备",
    "Device names": "设备名称",
    "Devices": "内网设备",
    "Devices of the local network": "内网设备",
    "Domain": "域名",
    "Domain cache lifetime": "域名缓存时间",
    "Download": "下载",
    "Download rate": "下载速率",
    "Downloaded": "下载量",
    "Downloaded in this period": "本周期下载量",
    "Grant access to the traffic statistics": "授予流量统计接口访问权限",
    "History": "历史统计",
    "History retention": "历史保留天数",
    "IP address": "IP 地址",
    "Interface": "接口",
    "Interface whose byte counters are shown as the total traffic of the internet "
    "connection.": "用于显示外网连接总流量的接口字节计数器。",
    "Internet connection": "外网连接",
    "Known domains": "已知域名",
    "LAN interfaces": "内网接口",
    "Length of a sampling interval in seconds. Lower values are more accurate but "
    "use a bit more CPU.": "采样间隔，单位为秒。数值越小越精确，但会占用更多 CPU。",
    "Loading ...": "加载中 ...",
    "MAC address": "MAC 地址",
    "No traffic recorded yet.": "暂无流量记录。",
    "Number of days the daily statistics are kept on the flash memory.":
        "每日统计在闪存中保留的天数。",
    "Number of seconds a learned domain name is kept before it is looked up again.":
        "已学习到的域名在重新查询前的保留时间，单位为秒。",
    "Optional friendly name for a device, in the form \"mac address=name\".":
        "为设备设置易读名称，格式为 \"MAC 地址=名称\"。",
    "Overview": "总览",
    "Realtime": "实时",
    "Realtime traffic": "实时流量",
    "Router itself": "OpenWRT 自身",
    "Sampling interval": "采样间隔",
    "Settings": "设置",
    "Statistics interface": "统计接口",
    "The addresses and domains of the internet that are used the most. The domain "
    "names are learned from the DNS traffic of the router.":
        "使用最多的外网地址与域名。域名由路由器的 DNS 流量中学习得到。",
    "The collector reads the traffic of the router with a read only packet "
    "socket. No firewall rule, route or DNS setting is changed.":
        "采集器以只读方式读取路由器上的流量，不会修改任何防火墙规则、路由或 DNS 设置。",
    "The devices with the highest upload and download volume. The router itself "
    "is listed as well.":
        "上传下载量最高的内网设备，同时包含 OpenWRT 自身。",
    "Top 10 addresses": "外网 IP 流量 Top 10",
    "Top 10 devices": "内网设备 Top 10",
    "Top 10 domains": "外网域名 Top 10",
    "Total": "合计",
    "Total downloaded": "累计下载",
    "Total uploaded": "累计上传",
    "Traffic Statistics": "流量统计",
    "Traffic history": "流量历史",
    "Upload": "上传",
    "Upload rate": "上传速率",
    "Uploaded": "上传量",
    "Uploaded in this period": "本周期上传量",
    "Uptime": "运行时长",
    "WAN interfaces": "外网接口",
    "d": "天",
    "h": "时",
    "m": "分",
    "realtime": "实时",
    "s": "秒",
    "since the collector started": "自采集器启动以来",
}

STRING_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')


def unescape(text):
    return text.replace('\\"', '"').replace("\\n", "\n").replace("\\t", "\t").replace("\\\\", "\\")


def escape(text):
    return text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")


def parse(path):
    entries = []
    with open(path, encoding="utf-8") as handle:
        for block in handle.read().split("\n\n"):
            lines = [line for line in block.split("\n") if line.strip()]
            if not lines:
                continue

            comments = [line for line in lines if line.startswith("#")]
            body = [line for line in lines if not line.startswith("#")]

            msgid = None
            msgstr = None
            current = None
            for line in body:
                if line.startswith("msgid "):
                    current = "id"
                    msgid = "".join(STRING_RE.findall(line))
                elif line.startswith("msgstr "):
                    current = "str"
                    msgstr = "".join(STRING_RE.findall(line))
                elif line.startswith('"'):
                    chunk = "".join(STRING_RE.findall(line))
                    if current == "id":
                        msgid += chunk
                    elif current == "str":
                        msgstr += chunk

            if msgid is None:
                continue

            entries.append((comments, unescape(msgid), unescape(msgstr or "")))
    return entries


def main():
    pot, po, lang = sys.argv[1], sys.argv[2], sys.argv[3]
    entries = parse(pot)
    missing = []

    with open(po, "w", encoding="utf-8") as handle:
        handle.write('# Chinese translation of luci-app-traffic-stats\n')
        handle.write('# Copyright (C) 2026 luci-app-traffic-stats contributors\n')
        handle.write('# This file is distributed under the same license as the package.\n')
        handle.write('#\n')
        handle.write('msgid ""\n')
        handle.write('msgstr ""\n')
        handle.write('"Project-Id-Version: luci-app-traffic-stats 1.0.0\\n"\n')
        handle.write('"PO-Revision-Date: 2026-01-01 00:00+0000\\n"\n')
        handle.write('"Last-Translator: luci-app-traffic-stats contributors\\n"\n')
        handle.write('"Language-Team: Chinese (Simplified)\\n"\n')
        handle.write('"Language: %s\\n"\n' % lang)
        handle.write('"MIME-Version: 1.0\\n"\n')
        handle.write('"Content-Type: text/plain; charset=UTF-8\\n"\n')
        handle.write('"Content-Transfer-Encoding: 8bit\\n"\n')
        handle.write('\n')

        for comments, msgid, _ in entries:
            if msgid == "":
                continue

            translation = TRANSLATIONS.get(msgid)
            if translation is None:
                missing.append(msgid)
                translation = ""

            for comment in comments:
                handle.write(comment + "\n")
            handle.write('msgid "%s"\n' % escape(msgid))
            handle.write('msgstr "%s"\n\n' % escape(translation))

    if missing:
        sys.stderr.write("missing translations:\n")
        for item in missing:
            sys.stderr.write("  %s\n" % item)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())

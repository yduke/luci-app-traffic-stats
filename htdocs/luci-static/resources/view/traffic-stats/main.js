'use strict';
'require view';
'require rpc';
'require poll';
'require uci';
'require ui';
'require form';

var callOverview = rpc.declare({
	object: 'luci.trafficstats',
	method: 'overview',
	expect: { '': {} }
});

var callDevices = rpc.declare({
	object: 'luci.trafficstats',
	method: 'devices',
	params: [ 'range' ],
	expect: { '': {} }
});

var callDestinations = rpc.declare({
	object: 'luci.trafficstats',
	method: 'destinations',
	params: [ 'range' ],
	expect: { '': {} }
});

var callHistory = rpc.declare({
	object: 'luci.trafficstats',
	method: 'history',
	params: [ 'range' ],
	expect: { '': {} }
});

var callRestart = rpc.declare({
	object: 'luci.trafficstats',
	method: 'restart',
	expect: { '': {} }
});

var POLL_INTERVAL = 2;
var LIVE_SAMPLES = 150;

var RANGES = [
	{ id: '24h', label: _('24 hours') },
	{ id: '7d', label: _('7 days') },
	{ id: '30d', label: _('30 days') },
	{ id: '1y', label: _('1 year') }
];

function formatBytes(value) {
	var units = [ 'B', 'KB', 'MB', 'GB', 'TB', 'PB' ];
	var i = 0;
	var v = Number(value) || 0;

	while (v >= 1024 && i < units.length - 1) {
		v /= 1024;
		i++;
	}

	return (i === 0 ? v : v.toFixed(2)) + ' ' + units[i];
}

function formatRate(value) {
	var units = [ 'B/s', 'KB/s', 'MB/s', 'GB/s' ];
	var i = 0;
	var v = Number(value) || 0;

	while (v >= 1024 && i < units.length - 1) {
		v /= 1024;
		i++;
	}

	return (i === 0 ? v : v.toFixed(2)) + ' ' + units[i];
}

function formatNumber(value) {
	return (Number(value) || 0).toLocaleString();
}

function formatUptime(seconds) {
	var d = Math.floor(seconds / 86400);
	var h = Math.floor((seconds % 86400) / 3600);
	var m = Math.floor((seconds % 3600) / 60);
	var s = Math.floor(seconds % 60);

	if (d > 0)
		return d + _('d') + ' ' + h + _('h') + ' ' + m + _('m');
	if (h > 0)
		return h + _('h') + ' ' + m + _('m');
	if (m > 0)
		return m + _('m') + ' ' + s + _('s');

	return s + _('s');
}

function formatTime(ts) {
	var d = new Date(Number(ts) * 1000);
	var pad = function(n) { return (n < 10 ? '0' : '') + n; };

	return d.getFullYear() + '-' + pad(d.getMonth() + 1) + '-' + pad(d.getDate()) +
		' ' + pad(d.getHours()) + ':' + pad(d.getMinutes());
}

/*
 * Draw a two line area chart. The chart uses a fixed coordinate system so
 * it scales with the browser window without any JavaScript resizing.
 */
function drawChart(svg, series, options) {
	var W = 1000, H = 260;
	var padL = 62, padR = 12, padT = 14, padB = 26;
	var max = 0;
	var count = 0;
	var i, j;

	options = options || {};

	while (svg.firstChild)
		svg.removeChild(svg.firstChild);

	svg.setAttribute('viewBox', '0 0 ' + W + ' ' + H);
	svg.setAttribute('preserveAspectRatio', 'none');

	for (i = 0; i < series.length; i++) {
		count = Math.max(count, series[i].values.length);
		for (j = 0; j < series[i].values.length; j++)
			max = Math.max(max, series[i].values[j]);
	}

	if (max <= 0)
		max = 1;

	var maxLabel = options.label || '';
	var scaleMax = max * 1.15;

	var ns = 'http://www.w3.org/2000/svg';

	function el(name, attrs) {
		var node = document.createElementNS(ns, name);
		for (var k in attrs)
			node.setAttribute(k, attrs[k]);
		return node;
	}

	function y(value) {
		return padT + (H - padT - padB) * (1 - (value / scaleMax));
	}

	function x(index) {
		if (count <= 1)
			return padL;
		return padL + (W - padL - padR) * (index / (count - 1));
	}

	/* horizontal grid */
	var steps = 4;

	for (i = 0; i <= steps; i++) {
		var value = (scaleMax / steps) * i;
		var yy = y(value);

		svg.appendChild(el('line', {
			x1: padL, y1: yy, x2: W - padR, y2: yy,
			class: 'ts-grid'
		}));

		var label = el('text', { x: padL - 6, y: yy + 4, class: 'ts-axis', 'text-anchor': 'end' });
		label.appendChild(document.createTextNode(options.format ? options.format(value) : formatBytes(value)));
		svg.appendChild(label);
	}

	/* vertical grid */
	for (i = 0; i <= 4; i++) {
		var xx = padL + (W - padL - padR) * (i / 4);
		svg.appendChild(el('line', {
			x1: xx, y1: padT, x2: xx, y2: H - padB, class: 'ts-grid'
		}));
	}

	/* the series */
	for (i = 0; i < series.length; i++) {
		var values = series[i].values;
		var points = [];
		var area = [];

		for (j = 0; j < values.length; j++)
			points.push(x(j).toFixed(1) + ',' + y(values[j]).toFixed(1));

		if (points.length > 0) {
			area.push(padL + ',' + (H - padB));
			area = area.concat(points);
			area.push(x(points.length - 1).split(',')[0] + ',' + (H - padB));

			svg.appendChild(el('polygon', {
				points: area.join(' '), class: 'ts-area ts-area-' + series[i].key
			}));

			svg.appendChild(el('polyline', {
				points: points.join(' '), class: 'ts-line ts-line-' + series[i].key
			}));
		}
	}

	/* x axis labels */
	if (options.labels && options.labels.length) {
		for (i = 0; i < options.labels.length; i++) {
			var lx = padL + (W - padL - padR) * (i / Math.max(1, options.labels.length - 1));
			var lbl = el('text', { x: lx, y: H - 8, class: 'ts-axis', 'text-anchor': i === 0 ? 'start' : 'middle' });
			lbl.appendChild(document.createTextNode(options.labels[i]));
			svg.appendChild(lbl);
		}
	}

	if (maxLabel) {
		var title = el('text', { x: padL, y: 11, class: 'ts-axis', 'text-anchor': 'start' });
		title.appendChild(document.createTextNode(maxLabel));
		svg.appendChild(title);
	}
}

function statBox(label, value, unit, cls) {
	return E('div', { 'class': 'ts-stat ' + (cls || '') }, [
		E('div', { 'class': 'ts-stat-label' }, label),
		E('div', { 'class': 'ts-stat-value' }, [
			E('span', { 'class': 'ts-stat-number' }, value),
			unit ? E('span', { 'class': 'ts-stat-unit' }, unit) : ''
		]),
		E('div', { 'class': 'ts-stat-sub' }, '')
	]);
}

function table(headers, rows, cls) {
	var thead = E('tr', {}, headers.map(function(h) {
		return E('th', { 'class': h.cls || '' }, h.title);
	}));

	var tbody = rows.map(function(row, idx) {
		return E('tr', { 'class': row.cls || '' }, row.cells.map(function(cell, i) {
			return E('td', { 'class': (headers[i] && headers[i].cls) || '' }, cell);
		}));
	});

	return E('div', { 'class': 'ts-table-wrap' }, [
		E('table', { 'class': 'table ts-table ' + (cls || '') }, [
			E('thead', {}, thead),
			E('tbody', {}, tbody)
		])
	]);
}

return view.extend({
	load: function() {
		return Promise.all([
			uci.load('traffic-stats'),
			callOverview().catch(function() { return null; })
		]);
	},

	render: function(data) {
		var self = this;
		var cfg = (data && data[0]) || {};
		var state = {
			live: (data && data[1]) || null,
			range: '24h',
			series: [],
			tab: 'overview'
		};

		/* the stylesheet of the application */
		if (!document.querySelector('link[data-traffic-stats-css]')) {
			var link = document.createElement('link');
			link.rel = 'stylesheet';
			link.type = 'text/css';
			link.setAttribute('data-traffic-stats-css', '1');
			link.href = L.resource('traffic-stats/traffic-stats.css');
			document.head.appendChild(link);
		}

		var container = E('div', { 'class': 'ts-root' });

		/* ---------------- status bar ---------------- */

		var statusText = E('span', { 'class': 'ts-status-text' }, _('Loading ...'));
		var statusBox = E('div', { 'class': 'ts-status' }, [
			E('span', { 'class': 'ts-dot' }),
			statusText
		]);

		var infoBox = E('div', { 'class': 'ts-info' });

		container.appendChild(E('div', { 'class': 'ts-toolbar' }, [
			statusBox,
			infoBox
		]));

		/* ---------------- tabs ---------------- */

		var tabs = E('div', { 'class': 'ts-tabs' });
		var panes = E('div', { 'class': 'ts-panes' });

		var tabDefs = [
			{ id: 'overview', title: _('Overview') },
			{ id: 'devices', title: _('Devices') },
			{ id: 'destinations', title: _('Destinations') },
			{ id: 'history', title: _('History') },
			{ id: 'settings', title: _('Settings') }
		];

		var paneMap = {};

		tabDefs.forEach(function(def) {
			var button = E('button', {
				'type': 'button',
				'class': 'ts-tab',
				'data-tab': def.id,
				click: function() {
					selectTab(def.id);
				}
			}, def.title);

			tabs.appendChild(button);

			var pane = E('div', { 'class': 'ts-pane', 'data-pane': def.id });
			paneMap[def.id] = pane;
			panes.appendChild(pane);
		});

		function selectTab(id) {
			state.tab = id;

			tabs.querySelectorAll('.ts-tab').forEach(function(btn) {
				btn.classList.toggle('ts-tab-active', btn.getAttribute('data-tab') === id);
			});

			Object.keys(paneMap).forEach(function(key) {
				paneMap[key].classList.toggle('ts-pane-active', key === id);
			});

			if (id === 'devices')
				refreshDevices();
			else if (id === 'destinations')
				refreshDestinations();
			else if (id === 'history')
				refreshHistory();
		}

		container.appendChild(tabs);
		container.appendChild(panes);

		/* ---------------- overview pane ---------------- */

		var downBox = statBox(_('Download'), '0', '', 'ts-down');
		var upBox = statBox(_('Upload'), '0', '', 'ts-up');
		var downTotal = statBox(_('Total downloaded'), '0', '', 'ts-down');
		var upTotal = statBox(_('Total uploaded'), '0', '', 'ts-up');

		var liveChart = E('svg', { 'class': 'ts-chart', 'preserveAspectRatio': 'none' });
		var liveChartBox = E('div', { 'class': 'ts-card' }, [
			E('div', { 'class': 'ts-card-title' }, _('Realtime traffic')),
			E('div', { 'class': 'ts-legend' }, [
				E('span', { 'class': 'ts-legend-item ts-legend-down' }, _('Download')),
				E('span', { 'class': 'ts-legend-item ts-legend-up' }, _('Upload'))
			]),
			E('div', { 'class': 'ts-chart-wrap' }, liveChart)
		]);

		var wanTable = E('div', { 'class': 'ts-card' }, [
			E('div', { 'class': 'ts-card-title' }, _('Internet connection'))
		]);

		paneMap.overview.appendChild(E('div', { 'class': 'ts-grid ts-grid-4' }, [
			downBox, upBox, downTotal, upTotal
		]));
		paneMap.overview.appendChild(liveChartBox);
		paneMap.overview.appendChild(wanTable);

		/* ---------------- devices pane ---------------- */

		var devicesRange = E('div', { 'class': 'ts-range' });
		var devicesBody = E('div', { 'class': 'ts-card' });

		paneMap.devices.appendChild(E('div', { 'class': 'ts-card' }, [
			E('div', { 'class': 'ts-card-title' }, _('Devices of the local network')),
			E('div', { 'class': 'ts-card-desc' },
				_('The devices with the highest upload and download volume. The router itself is listed as well.')),
			devicesRange
		]));
		paneMap.devices.appendChild(devicesBody);

		/* ---------------- destinations pane ---------------- */

		var destRange = E('div', { 'class': 'ts-range' });
		var destBody = E('div', { 'class': 'ts-card' });

		paneMap.destinations.appendChild(E('div', { 'class': 'ts-card' }, [
			E('div', { 'class': 'ts-card-title' }, _('Destinations on the internet')),
			E('div', { 'class': 'ts-card-desc' },
				_('The addresses and domains of the internet that are used the most. The domain names are learned from the DNS traffic of the router.')),
			destRange
		]));
		paneMap.destinations.appendChild(destBody);

		/* ---------------- history pane ---------------- */

		var historyRange = E('div', { 'class': 'ts-range' });
		var historyChart = E('svg', { 'class': 'ts-chart', 'preserveAspectRatio': 'none' });
		var historyTotals = E('div', { 'class': 'ts-grid ts-grid-2' });

		paneMap.history.appendChild(E('div', { 'class': 'ts-card' }, [
			E('div', { 'class': 'ts-card-title' }, _('Traffic history')),
			historyRange
		]));
		paneMap.history.appendChild(E('div', { 'class': 'ts-card' }, [
			E('div', { 'class': 'ts-legend' }, [
				E('span', { 'class': 'ts-legend-item ts-legend-down' }, _('Download')),
				E('span', { 'class': 'ts-legend-item ts-legend-up' }, _('Upload'))
			]),
			E('div', { 'class': 'ts-chart-wrap' }, historyChart)
		]));
		paneMap.history.appendChild(historyTotals);

		/* ---------------- settings pane ---------------- */

		var map = new form.Map('traffic-stats', _('Settings'),
			_('The collector reads the traffic of the router with a read only packet socket. No firewall rule, route or DNS setting is changed.'));

		var section = map.section(form.NamedSection, 'global', _('Collector'));
		var opt;

		opt = section.option(form.Value, 'lan_interfaces', _('LAN interfaces'),
			_('Comma separated list of the interfaces that face the local network. The traffic of every device is attributed here.'));
		opt.default = 'br-lan';

		opt = section.option(form.Value, 'wan_interfaces', _('WAN interfaces'),
			_('Comma separated list of the interfaces that face the internet. They provide the total traffic volume and the destination statistics.'));
		opt.default = 'wan';

		opt = section.option(form.Value, 'stats_iface', _('Statistics interface'),
			_('Interface whose byte counters are shown as the total traffic of the internet connection.'));
		opt.default = 'pppoe-wan';

		opt = section.option(form.Value, 'sample_interval', _('Sampling interval'),
			_('Length of a sampling interval in seconds. Lower values are more accurate but use a bit more CPU.'));
		opt.default = '1';
		opt.datatype = 'range(1,60)';

		opt = section.option(form.Value, 'dns_ttl', _('Domain cache lifetime'),
			_('Number of seconds a learned domain name is kept before it is looked up again.'));
		opt.default = '300';
		opt.datatype = 'range(10,86400)';

		opt = section.option(form.Flag, 'count_internal', _('Count internal traffic'),
			_('Also account for the traffic between the devices of the local network.'));
		opt.default = '0';

		opt = section.option(form.Value, 'history_days', _('History retention'),
			_('Number of days the daily statistics are kept on the flash memory.'));
		opt.default = '400';
		opt.datatype = 'range(1,3650)';

		opt = section.option(form.DynamicList, 'alias', _('Device names'),
			_('Optional friendly name for a device, in the form "mac address=name".'));
		opt.placeholder = 'b0:7b:25:44:b1:16=Living room TV';

		var formHolder = E('div', { 'class': 'ts-form-holder' });

		paneMap.settings.appendChild(formHolder);

		/* the form is rendered asynchronously */
		map.render().then(function(node) {
			formHolder.appendChild(node);
		}).catch(function() {
			formHolder.appendChild(E('div', { 'class': 'ts-empty' },
				_('The settings could not be loaded.')));
		});

		var restartButton = E('button', {
			'class': 'btn cbi-button cbi-button-action',
			'type': 'button',
			click: function() {
				restartButton.setAttribute('disabled', 'disabled');

				callRestart().then(function() {
					restartButton.removeAttribute('disabled');
					refreshOverview();
				}).catch(function() {
					restartButton.removeAttribute('disabled');
				});
			}
		}, _('Restart collector'));

		paneMap.settings.appendChild(E('div', { 'class': 'ts-card' }, [
			E('div', { 'class': 'ts-card-title' }, _('Collector')),
			E('div', { 'class': 'ts-card-desc' },
				_('The collector has to be restarted after the settings were changed.')),
			E('div', { 'class': 'ts-button-row' }, restartButton)
		]));

		/* ---------------- range selectors ---------------- */

		function renderRange(container, current, onChange) {
			container.innerHTML = '';

			RANGES.forEach(function(r) {
				container.appendChild(E('button', {
					'type': 'button',
					'class': 'ts-range-btn' + (r.id === current ? ' ts-range-active' : ''),
					click: function() {
						renderRange(container, r.id, onChange);
						onChange(r.id);
					}
				}, r.label));
			});
		}

		/* ---------------- data loading ---------------- */

		function setStat(box, value, unit, sub) {
			box.querySelector('.ts-stat-number').textContent = value;
			box.querySelector('.ts-stat-unit').textContent = unit || '';
			box.querySelector('.ts-stat-sub').textContent = sub || '';
		}

		function updateOverview(live) {
			if (!live || !live.available)
				return;

			state.live = live;

			var wan = live.wan || {};
			var down = Number(wan.down_rate) || 0;
			var up = Number(wan.up_rate) || 0;

			setStat(downBox, formatRate(down).split(' ')[0], formatRate(down).split(' ').slice(1).join(' '),
				_('realtime'));
			setStat(upBox, formatRate(up).split(' ')[0], formatRate(up).split(' ').slice(1).join(' '),
				_('realtime'));
			setStat(downTotal, formatBytes(wan.down_bytes).split(' ')[0],
				formatBytes(wan.down_bytes).split(' ').slice(1).join(' '),
				_('since the collector started'));
			setStat(upTotal, formatBytes(wan.up_bytes).split(' ')[0],
				formatBytes(wan.up_bytes).split(' ').slice(1).join(' '),
				_('since the collector started'));

			/* live chart */
			state.series.push({ down: down, up: up });
			while (state.series.length > LIVE_SAMPLES)
				state.series.shift();

			drawChart(liveChart, [
				{ key: 'down', values: state.series.map(function(s) { return s.down; }) },
				{ key: 'up', values: state.series.map(function(s) { return s.up; }) }
			], { format: formatRate });

			/* connection details */
			wanTable.innerHTML = '';
			wanTable.appendChild(E('div', { 'class': 'ts-card-title' }, _('Internet connection')));

			var rows = [
				{
					cells: [ _('Interface'), wan.iface || '-' ]
				},
				{
					cells: [ _('Total downloaded'), formatBytes(wan.rx_bytes) ]
				},
				{
					cells: [ _('Total uploaded'), formatBytes(wan.tx_bytes) ]
				},
				{
					cells: [ _('Download rate'), formatRate(wan.rx_rate) ]
				},
				{
					cells: [ _('Upload rate'), formatRate(wan.tx_rate) ]
				},
				{
					cells: [ _('Router itself'), formatBytes((live.router || {}).down_bytes) + ' / ' + formatBytes((live.router || {}).up_bytes) ]
				},
				{
					cells: [ _('Collected packets'), formatNumber((live.counters || {}).packets) ]
				},
				{
					cells: [ _('Known domains'), formatNumber((live.dns || {}).entries) ]
				}
			];

			wanTable.appendChild(table(
				[ { title: '' }, { title: '' } ],
				rows.map(function(r) {
					return { cells: r.cells };
				})
			));

			statusText.textContent = _('Collector running');
			infoBox.textContent = _('Uptime') + ': ' + formatUptime(live.uptime) + '  ·  ' +
				_('Interface') + ': ' + ((live.interfaces && live.interfaces.lan) || []).join(', ');
		}

		function refreshOverview() {
			return callOverview().then(function(live) {
				updateOverview(live);
			}).catch(function() {
				statusText.textContent = _('Collector is not running');
			});
		}

		function deviceRows(list) {
			var rows = [];

			list.slice(0, 10).forEach(function(d) {
				rows.push({
					cls: d.live ? 'ts-row-live' : '',
					cells: [
						d.name || d.mac,
						d.ip || '-',
						d.mac || '-',
						formatBytes(d.down),
						formatBytes(d.up),
						formatBytes((Number(d.down) || 0) + (Number(d.up) || 0)),
						formatRate(d.down_rate) + ' / ' + formatRate(d.up_rate)
					]
				});
			});

			return rows;
		}

		function refreshDevices() {
			return callDevices(state.range).then(function(res) {
				var list = (res && res.devices) || [];

				devicesBody.innerHTML = '';
				devicesBody.appendChild(E('div', { 'class': 'ts-card-title' },
					_('Top 10 devices') + ' (' + list.length + ')'));

				if (!list.length) {
					devicesBody.appendChild(E('div', { 'class': 'ts-empty' },
						_('No traffic recorded yet.')));
					return;
				}

				devicesBody.appendChild(table([
					{ title: _('Device') },
					{ title: _('IP address') },
					{ title: _('MAC address') },
					{ title: _('Downloaded'), cls: 'ts-num' },
					{ title: _('Uploaded'), cls: 'ts-num' },
					{ title: _('Total'), cls: 'ts-num' },
					{ title: _('Realtime'), cls: 'ts-num' }
				], deviceRows(list)));
			}).catch(function() {});
		}

		function refreshDestinations() {
			return callDestinations(state.range).then(function(res) {
				var dests = (res && res.destinations) || [];
				var domains = (res && res.domains) || [];

				destBody.innerHTML = '';

				destBody.appendChild(E('div', { 'class': 'ts-card-title' },
					_('Top 10 domains') + ' (' + domains.length + ')'));

				if (!domains.length) {
					destBody.appendChild(E('div', { 'class': 'ts-empty' },
						_('No traffic recorded yet.')));
					return;
				}

				destBody.appendChild(table([
					{ title: _('Domain') },
					{ title: _('Addresses'), cls: 'ts-num' },
					{ title: _('Downloaded'), cls: 'ts-num' },
					{ title: _('Uploaded'), cls: 'ts-num' },
					{ title: _('Total'), cls: 'ts-num' }
				], domains.slice(0, 10).map(function(d) {
					return {
						cells: [
							d.domain,
							formatNumber(d.addresses),
							formatBytes(d.down),
							formatBytes(d.up),
							formatBytes((Number(d.down) || 0) + (Number(d.up) || 0))
						]
					};
				})));

				destBody.appendChild(E('div', { 'class': 'ts-card-title' },
					_('Top 10 addresses')));

				destBody.appendChild(table([
					{ title: _('Address') },
					{ title: _('Domain') },
					{ title: _('Downloaded'), cls: 'ts-num' },
					{ title: _('Uploaded'), cls: 'ts-num' },
					{ title: _('Total'), cls: 'ts-num' }
				], dests.slice(0, 10).map(function(d) {
					return {
						cells: [
							d.ip,
							d.domain || '-',
							formatBytes(d.down),
							formatBytes(d.up),
							formatBytes((Number(d.down) || 0) + (Number(d.up) || 0))
						]
					};
				})));
			}).catch(function() {});
		}

		function refreshHistory() {
			return callHistory(state.range).then(function(res) {
				var points = (res && res.points) || [];

				historyTotals.innerHTML = '';
				historyTotals.appendChild(statBox(_('Downloaded in this period'),
					formatBytes((res && res.down_total) || 0).split(' ')[0],
					formatBytes((res && res.down_total) || 0).split(' ').slice(1).join(' '), 'ts-down'));
				historyTotals.appendChild(statBox(_('Uploaded in this period'),
					formatBytes((res && res.up_total) || 0).split(' ')[0],
					formatBytes((res && res.up_total) || 0).split(' ').slice(1).join(' '), 'ts-up'));

				var labels = [];
				var step = Math.max(1, Math.floor(points.length / 4));

				for (var i = 0; i < points.length; i += step)
					labels.push(formatTime(points[i].ts));

				drawChart(historyChart, [
					{ key: 'down', values: points.map(function(p) { return Number(p.down) || 0; }) },
					{ key: 'up', values: points.map(function(p) { return Number(p.up) || 0; }) }
				], { labels: labels, format: formatBytes });
			}).catch(function() {});
		}

		renderRange(devicesRange, state.range, function(range) {
			state.range = range;
			refreshDevices();
		});

		renderRange(destRange, state.range, function(range) {
			state.range = range;
			refreshDestinations();
		});

		renderRange(historyRange, state.range, function(range) {
			state.range = range;
			refreshHistory();
		});

		/* ---------------- polling ---------------- */

		poll.add(function() {
			return refreshOverview();
		}, POLL_INTERVAL);

		selectTab('overview');
		refreshOverview();
		refreshDevices();
		refreshDestinations();

		this.refreshDevices = refreshDevices;
		this.refreshDestinations = refreshDestinations;
		this.refreshHistory = refreshHistory;

		return container;
	}
});

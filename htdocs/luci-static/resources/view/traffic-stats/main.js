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

var callService = rpc.declare({
	object: 'luci.trafficstats',
	method: 'service',
	params: [ 'action' ],
	expect: { '': {} }
});

var callReset = rpc.declare({
	object: 'luci.trafficstats',
	method: 'reset',
	expect: { '': {} }
});

var callNetworkDevices = rpc.declare({
	object: 'network.device',
	method: 'status',
	expect: { '': {} }
});

var APP_VERSION = '1.0.0';

var POLL_INTERVAL = 2;
var POLL_IDLE_INTERVAL = 15;
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

/*
 * Bandwidth in bits per second. Below one megabit the value is shown in
 * kbps, above it in Mbps.
 */
function formatBitRate(bytesPerSecond) {
	var bits = (Number(bytesPerSecond) || 0) * 8;

	if (bits < 1000000)
		return { value: (bits / 1000).toFixed(bits < 10000 ? 1 : 0), unit: 'kbps' };

	if (bits < 1000000000)
		return { value: (bits / 1000000).toFixed(2), unit: 'Mbps' };

	return { value: (bits / 1000000000).toFixed(2), unit: 'Gbps' };
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
 * The E() helper of LuCI creates HTML elements, an SVG element has to be
 * created with its own namespace, otherwise the browser does not render
 * the chart.
 */
function createChart() {
	var svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');

	svg.setAttribute('class', 'ts-chart');
	svg.setAttribute('preserveAspectRatio', 'none');

	return svg;
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

		/* a single sample is drawn across the whole width */
		if (points.length == 1)
			points.push((W - padR).toFixed(1) + ',' + y(values[0]).toFixed(1));

		if (points.length > 0) {
			area.push(padL.toFixed(1) + ',' + (H - padB).toFixed(1));
			area = area.concat(points);
			area.push(x(points.length - 1).toFixed(1) + ',' + (H - padB).toFixed(1));

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
			E('span', { 'class': 'ts-stat-unit' }, unit || '')
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
			link.href = L.resource('traffic-stats/traffic-stats.css') + '?v=' + APP_VERSION;
			document.head.appendChild(link);
		}

		var container = E('div', { 'class': 'ts-root' });
		var content = E('div', { 'class': 'ts-content' });

		container.appendChild(content);

		/* ---------------- status bar ---------------- */

		var statusText = E('span', { 'class': 'ts-status-text' }, _('Loading ...'));
		var statusBox = E('div', { 'class': 'ts-status' }, [
			E('span', { 'class': 'ts-dot' }),
			statusText
		]);

		var infoBox = E('div', { 'class': 'ts-info' });

		/* ---------------- tabs ---------------- */

		/*
		 * The tab panes use the standard LuCI mechanism, the theme
		 * renders the tab menu of the page in its own navigation bar.
		 */
		var tabDefs = [
			{ id: 'overview', title: _('Overview') },
			{ id: 'devices', title: _('Devices') },
			{ id: 'destinations', title: _('Destinations') },
			{ id: 'history', title: _('History') },
			{ id: 'settings', title: _('Settings') }
		];

		var paneMap = {};
		var paneNodes = [];

		function onTabActivated(id) {
			if (id === 'devices')
				refreshDevices();
			else if (id === 'destinations')
				refreshDestinations();
			else if (id === 'history')
				refreshHistory();
		}

		/* the status is shown below the tab menu of the page */
		content.appendChild(E('div', { 'class': 'ts-toolbar' }, [
			statusBox,
			infoBox
		]));

		tabDefs.forEach(function(def) {
			var pane = E('div', {
				'class': 'ts-pane',
				'data-tab': def.id,
				'data-tab-title': def.title,
				'data-tab-active': (def.id == 'overview') ? 'true' : null
			});

			pane.addEventListener('cbi-tab-active', function() {
				onTabActivated(def.id);
			});

			paneMap[def.id] = pane;
			paneNodes.push(pane);
			content.appendChild(pane);
		});

		/* ---------------- overview pane ---------------- */

		var downBox = statBox(_('Download'), '0', '', 'ts-down');
		var upBox = statBox(_('Upload'), '0', '', 'ts-up');
		var downTotalBox = statBox(_('Total downloaded'), '0', '', 'ts-down');
		var upTotalBox = statBox(_('Total uploaded'), '0', '', 'ts-up');

		var liveChart = createChart();
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
			downBox, upBox, downTotalBox, upTotalBox
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
		var historyChart = createChart();
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

		var formHolder = E('div', { 'class': 'ts-form-holder' });

		paneMap.settings.appendChild(formHolder);

		/* the settings are built when the list of interfaces is known */
		callNetworkDevices().catch(function() { return {}; }).then(function(devices) {
			var names = Object.keys(devices || {}).sort();
			var map = new form.Map('traffic-stats', _('Settings'),
				_('The collector reads the traffic of the router with a read only packet socket. No firewall rule, route or DNS setting is changed.'));
			var section = map.section(form.NamedSection, 'global', _('Collector'));
			var opt;

			opt = section.option(form.RichListValue, 'lan_interfaces', _('LAN interfaces'),
				_('The interfaces that face the local network. The traffic of every device is attributed here.'));
			opt.multiple = true;
			opt.rmempty = false;
			names.forEach(function(name) { opt.value(name, name); });
			opt.default = [ 'br-lan' ];

			opt = section.option(form.RichListValue, 'wan_interfaces', _('WAN interfaces'),
				_('The interfaces that face the internet. They provide the total traffic volume and the destination statistics.'));
			opt.multiple = true;
			opt.rmempty = false;
			names.forEach(function(name) { opt.value(name, name); });
			opt.default = [ 'wan' ];

			opt = section.option(form.ListValue, 'stats_iface', _('Statistics interface'),
				_('Interface whose byte counters are shown as the total traffic of the internet connection.'));
			names.forEach(function(name) { opt.value(name, name); });
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

			return map.render();
		}).then(function(node) {
			formHolder.appendChild(node);
		}).catch(function() {
			formHolder.appendChild(E('div', { 'class': 'ts-empty' },
				_('The settings could not be loaded.')));
		});

		/* ---------------- collector control ---------------- */

		var collectorState = 'running';

		function collectorButton(label, cls, action) {
			var button = E('button', {
				'class': 'btn cbi-button ' + cls,
				'type': 'button',
				click: function() {
					button.setAttribute('disabled', 'disabled');

					callService(action).then(function() {
						button.removeAttribute('disabled');

						/* the state is known before the next poll */
						collectorState = (action == 'stop') ? 'stopped' : 'running';
						renderCollectorButtons();
						refreshOverview();
					}).catch(function(err) {
						button.removeAttribute('disabled');
						setError(err);
					});
				}
			}, label);

			return button;
		}

		function resetButton() {
			return E('button', {
				'class': 'btn cbi-button cbi-button-negative',
				'type': 'button',
				click: function() {
					ui.showModal(_('Reset statistics'), [
						E('p', {}, _('All collected statistics will be deleted and the counting starts from zero again.')),
						E('div', { 'class': 'right' }, [
							E('button', {
								'class': 'btn cbi-button',
								'click': ui.hideModal
							}, _('Cancel')),
							' ',
							E('button', {
								'class': 'btn cbi-button cbi-button-negative important',
								'click': function() {
									ui.hideModal();
									callReset().then(function() {
										refreshOverview();
										refreshDevices();
										refreshDestinations();
										refreshHistory();
									}).catch(function(err) {
										setError(err);
									});
								}
							}, _('Reset statistics'))
						])
					]);
				}
			}, _('Reset statistics'));
		}

		var collectorButtons = E('div', { 'class': 'ts-button-row' });

		function renderCollectorButtons() {
			collectorButtons.innerHTML = '';

			if (collectorState == 'running') {
				collectorButtons.appendChild(collectorButton(_('Restart collector'),
					'cbi-button-action', 'restart'));
				collectorButtons.appendChild(collectorButton(_('Stop collector'),
					'cbi-button', 'stop'));
			} else {
				collectorButtons.appendChild(collectorButton(_('Start collector'),
					'cbi-button-action', 'start'));
			}

			collectorButtons.appendChild(resetButton());
		}

		paneMap.settings.appendChild(E('div', { 'class': 'ts-card' }, [
			E('div', { 'class': 'ts-card-title' }, _('Collector')),
			E('div', { 'class': 'ts-card-desc' },
				_('The collector has to be restarted after the settings were changed. A stopped collector does not use any CPU time, the statistics of the last period are still shown.')),
			collectorButtons
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
			var number = box.querySelector('.ts-stat-number');
			var u = box.querySelector('.ts-stat-unit');
			var s = box.querySelector('.ts-stat-sub');

			if (number)
				number.textContent = value;

			if (u)
				u.textContent = unit || '';

			if (s)
				s.textContent = sub || '';
		}

		/*
		 * Show a problem in the status bar, the message helps to find the
		 * reason without a javascript console.
		 */
		function setError(err) {
			var msg = (err && err.message) ? err.message : String(err);

			statusText.textContent = _('Error') + ': ' + msg;
			statusBox.setAttribute('data-error', '1');
		}

		function clearError() {
			statusBox.removeAttribute('data-error');
		}

		/* errors outside of the update functions are shown as well */
		window.addEventListener('error', function(ev) {
			setError(ev.error || ev.message);
		});

		window.addEventListener('unhandledrejection', function(ev) {
			setError(ev.reason);
		});

		function updateOverview(live) {
			if (!live || !live.available)
				return;

			state.live = live;

			collectorState = live.collector || 'running';
			renderCollectorButtons();

			/* slow the page down when the collector is stopped */
			if (collectorState != state.collector) {
				state.collector = collectorState;
				window.setTimeout(function() {
					setPollInterval(collectorState == 'running'
						? POLL_INTERVAL : POLL_IDLE_INTERVAL);
				}, 0);
			}

			var wan = live.wan || {};
			var down = Number(wan.down_rate) || 0;
			var up = Number(wan.up_rate) || 0;
			var downRate = formatBitRate(down);
			var upRate = formatBitRate(up);
			var downTotal = formatBytes(wan.rx_bytes);
			var upTotal = formatBytes(wan.tx_bytes);

			clearError();

			setStat(downBox, downRate.value, downRate.unit, _('realtime'));
			setStat(upBox, upRate.value, upRate.unit, _('realtime'));
			setStat(downTotalBox, downTotal.split(' ')[0], downTotal.split(' ').slice(1).join(' '),
				_('since the connection was established'));
			setStat(upTotalBox, upTotal.split(' ')[0], upTotal.split(' ').slice(1).join(' '),
				_('since the connection was established'));

			/* live chart */
			state.series.push({ down: down, up: up });
			while (state.series.length > LIVE_SAMPLES)
				state.series.shift();

			try {
				drawChart(liveChart, [
					{ key: 'down', values: state.series.map(function(s) { return s.down; }) },
					{ key: 'up', values: state.series.map(function(s) { return s.up; }) }
				], { format: function(value) {
					var rate = formatBitRate(value);
					return rate.value + ' ' + rate.unit;
				} });
			} catch (e) {
				setError(e);
			}

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
					cells: [ _('Download rate'), formatBitRate(wan.rx_rate).value + ' ' + formatBitRate(wan.rx_rate).unit ]
				},
				{
					cells: [ _('Upload rate'), formatBitRate(wan.tx_rate).value + ' ' + formatBitRate(wan.tx_rate).unit ]
				},
				{
					cells: [ _('Collected by the collector'), formatBytes(wan.down_bytes) + ' / ' + formatBytes(wan.up_bytes) ]
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

			statusText.textContent = (collectorState == 'running')
				? _('Collector running') : _('Collector stopped');
			infoBox.textContent = _('Uptime') + ': ' + formatUptime(live.uptime) + '  ·  ' +
				_('Interface') + ': ' + ((live.interfaces && live.interfaces.lan) || []).join(', ');
		}

		function refreshOverview() {
			return callOverview().then(function(live) {
				updateOverview(live);
			}).catch(function(err) {
				setError(err);
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
			}).catch(function(err) {
				setError(err);
			});
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

				try {
					drawChart(historyChart, [
						{ key: 'down', values: points.map(function(p) { return Number(p.down) || 0; }) },
						{ key: 'up', values: points.map(function(p) { return Number(p.up) || 0; }) }
					], { labels: labels, format: formatBytes });
				} catch (e) {
					setError(e);
				}
			}).catch(function(err) {
				setError(err);
			});
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

		/*
		 * A stopped collector does not need to be polled every second,
		 * the page slows down as well.
		 *
		 * The poll module of LuCI expects the callback function when a
		 * poll is removed, poll.add() does not return the entry.
		 */
		var pollCallback = null;

		function setPollInterval(interval) {
			if (pollCallback != null) {
				poll.remove(pollCallback);
				pollCallback = null;
			}

			pollCallback = function() {
				return refreshOverview();
			};

			poll.add(pollCallback, interval);
		}

		setPollInterval(POLL_INTERVAL);

		/* the overview is the active tab of the page */
		refreshOverview();
		refreshDevices();
		refreshDestinations();

		this.refreshDevices = refreshDevices;
		this.refreshDestinations = refreshDestinations;
		this.refreshHistory = refreshHistory;
		this.refreshOverview = refreshOverview;

		/* the tab menu is built when the panes are filled */
		ui.tabs.initTabGroup(paneNodes);

		return container;
	},

	/*
	 * The reset button of the page clears the collected statistics, the
	 * counting starts from zero again.
	 */
	handleReset: function() {
		var self = this;

		return callReset().then(function() {
			self.refreshOverview();
			self.refreshDevices();
			self.refreshDestinations();
			self.refreshHistory();
		});
	}
});

/*
 * Smoke test for the LuCI view of luci-app-traffic-stats.
 *
 * The view is loaded with a minimal set of mocked LuCI globals, the
 * rendered DOM tree is inspected for the expected elements. This catches
 * syntax errors, wrong API usage and broken render code without a browser.
 *
 * Usage: node build/view-smoke-test.js
 */

'use strict';

const fs = require('fs');
const path = require('path');
const assert = require('assert');

const VIEW = path.join(__dirname, '..', 'htdocs', 'luci-static', 'resources',
	'view', 'traffic-stats', 'main.js');

/* ------------------------------------------------------------------ */
/* minimal DOM mock                                                    */
/* ------------------------------------------------------------------ */

let nodeCount = 0;

function E(tag, attrs, children) {
	nodeCount++;

	let node = {
		tag: tag,
		attrs: attrs || {},
		children: [],
		text: ''
	};

	[].concat(children === undefined ? [] : children).forEach(function(child) {
		if (child === null || child === undefined || child === false)
			return;
		if (typeof child === 'string' || typeof child === 'number')
			node.text += String(child);
		else if (child.children)
			node.children.push(child);
	});

	node.querySelector = function(selector) {
		return findChild(node, selector);
	};

	node.querySelectorAll = function(selector) {
		let out = [];

		walk(node, function(child) {
			if (matches(child, selector))
				out.push(child);
		});

		return out;
	};

	node.appendChild = function(child) {
		/* the real DOM throws when a non node is passed */
		if (!child || typeof child !== 'object' || child.tag === undefined)
			throw new TypeError("Failed to execute 'appendChild' on 'Node': parameter 1 is not of type 'Node'.");

		node.children.push(child);
		return child;
	};

	node.innerHTML = '';
	node.textContent = '';
	node.getAttribute = function(name) {
		return node.attrs[name];
	};
	node.setAttribute = function(name, value) {
		node.attrs[name] = value;
	};
	node.removeAttribute = function(name) {
		delete node.attrs[name];
	};

	/* the text of an element is stored in "text" */
	Object.defineProperty(node, 'textContent', {
		get: function() { return node.text; },
		set: function(value) {
			node.text = (value === null || value === undefined) ? '' : String(value);
			node.children = [];
		}
	});

	Object.defineProperty(node, 'innerHTML', {
		get: function() { return node.text; },
		set: function(value) {
			node.text = (value === null || value === undefined) ? '' : String(value);
			node.children = [];
		}
	});
	node.classList = {
		toggle: function() {},
		add: function() {},
		remove: function() {}
	};
	node.className = (attrs && attrs['class']) || '';
	node.isConnected = true;

	return node;
}

function walk(node, fn) {
	(node.children || []).forEach(function(child) {
		fn(child);
		walk(child, fn);
	});
}

function matches(node, selector) {
	let cls = (node.attrs && node.attrs['class']) || '';

	return cls.split(/\s+/).indexOf(selector.replace('.', '')) >= 0;
}

function findChild(node, selector) {
	let found = null;

	walk(node, function(child) {
		if (!found && matches(child, selector))
			found = child;
	});

	return found;
}

function findAll(node, selector) {
	let out = [];

	walk(node, function(child) {
		if (matches(child, selector))
			out.push(child);
	});

	return out;
}

/* ------------------------------------------------------------------ */
/* mocked LuCI modules                                                 */
/* ------------------------------------------------------------------ */

const calls = { rpc: {}, poll: [], form: null, uci: null, errors: [] };

const rpc = {
	declare: function(spec) {
		let method = spec.method;
		let params = spec.params || [];

		return function() {
			let args = [].slice.call(arguments);
			calls.rpc[method] = (calls.rpc[method] || 0) + 1;

			if (method === 'overview')
				return Promise.resolve(overviewData());
			if (method === 'devices')
				return Promise.resolve({ range: args[0] || '24h', devices: deviceData() });
			if (method === 'destinations')
				return Promise.resolve({ range: args[0] || '24h', destinations: destData(), domains: domainData() });
			if (method === 'history')
				return Promise.resolve(historyData());
			if (method === 'restart')
				return Promise.resolve({ result: 0 });

			return Promise.resolve({});
		};
	}
};

const poll = {
	add: function(fn, interval) {
		calls.poll.push({ fn: fn, interval: interval });
	}
};

const uci = {
	load: function() {
		return Promise.resolve({});
	},
	apply: function() {
		return Promise.resolve();
	}
};

const form = {
	Map: function() {
		this.section = function() {
			return {
				option: function() {
					return {
						default: null,
						datatype: null,
						rmempty: true,
						placeholder: null
					};
				}
			};
		};
		this.render = function() {
			return Promise.resolve(E('div', { 'class': 'cbi-section' }, 'settings form'));
		};
	},
	NamedSection: 'NamedSection',
	Value: 'Value',
	Flag: 'Flag',
	DynamicList: 'DynamicList'
};

const view = {
	extend: function(proto) {
		return proto;
	}
};

const L = {
	resource: function(p) { return '/luci-static/resources/' + p; },
	url: function(p) { return '/cgi-bin/luci/' + p; },
	env: {},
	require: function() {},
	bind: function(fn) { return fn; },
	resolveDefault: function(p, d) { return p; }
};

global.window = { addEventListener: function() {} };
global._ = function(s) { return s; };
global.E = E;
global.document = {
	head: { appendChild: function() {} },
	querySelector: function() { return null; },
	createElement: function() { return E('div'); },
	createElementNS: function(ns, tag) { return E(tag); },
	createTextNode: function(t) {
		let node = E('#text');
		node.text = String(t);
		return node;
	}
};
global.setTimeout = setTimeout;
global.clearTimeout = clearTimeout;

/* ------------------------------------------------------------------ */
/* test data                                                           */
/* ------------------------------------------------------------------ */

function overviewData() {
	/* use the data of a real router when it is available */
	if (process.argv[2] && fs.existsSync(process.argv[2]))
		return JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));

	return {
		available: true,
		ts: 1700000000,
		version: '1.0.0',
		uptime: 3600,
		interval: 1,
		wan: {
			iface: 'pppoe-wan',
			rx_bytes: 123456789,
			tx_bytes: 987654321,
			rx_rate: 1024,
			tx_rate: 2048,
			up_bytes: 500000,
			down_bytes: 1500000,
			up_rate: 100,
			down_rate: 200
		},
		router: { up_bytes: 100, down_bytes: 200 },
		devices: deviceData(),
		destinations: destData(),
		domains: domainData(),
		counters: { packets: 10, bytes: 20, errors: 0, dropped: 0 },
		dns: { queries: 1, responses: 2, entries: 3 },
		interfaces: { lan: [ 'br-lan' ], wan: [ 'wan' ] }
	};
}

function deviceData() {
	return [
		{ mac: 'b0:7b:25:44:b1:16', ip: '192.168.2.6', name: 'server', up: 1000, down: 2000, up_rate: 10, down_rate: 20 },
		{ mac: '70:b5:e8:8e:e4:02', ip: '192.168.2.123', name: '192.168.2.123', up: 500, down: 100, up_rate: 1, down_rate: 2 }
	];
}

function destData() {
	return [
		{ ip: '1.2.3.4', domain: 'example.com', up: 100, down: 200, up_rate: 1, down_rate: 2 },
		{ ip: '5.6.7.8', domain: null, up: 50, down: 60, up_rate: 0, down_rate: 0 }
	];
}

function domainData() {
	return [
		{ domain: 'example.com', up: 100, down: 200, addresses: 1 }
	];
}

function historyData() {
	let points = [];

	for (let i = 0; i < 60; i++)
		points.push({ ts: 1700000000 + i * 60, up: i * 100, down: i * 200 });

	return { range: '24h', step: 60, points: points, up_total: 1000, down_total: 2000 };
}

/* ------------------------------------------------------------------ */
/* run                                                                 */
/* ------------------------------------------------------------------ */

const source = fs.readFileSync(VIEW, 'utf8');
const factory = new Function('view', 'rpc', 'poll', 'uci', 'ui', 'form', 'L', source);
const proto = factory(view, rpc, poll, uci, {}, form, L);

assert.ok(proto, 'the view module returned nothing');
assert.ok(typeof proto.render === 'function', 'the view has no render function');

const instance = Object.create(proto);
instance.render = proto.render.bind(instance);

const container = proto.render.call(instance, [ {}, overviewData() ]);

assert.ok(container, 'render() returned no container');
assert.ok(container.children.length > 0, 'the rendered container is empty');

/* the poll must be registered */
assert.ok(calls.poll.length > 0, 'no poll was registered');
assert.ok(calls.poll[0].interval >= 1, 'the poll interval is invalid');

/* run the poll callback once, it must not throw */
return Promise.resolve(calls.poll[0].fn()).then(function() {
	assert.ok(calls.rpc.overview > 0, 'the overview was never requested');

	/* the view reports problems in the status bar, the test must not see any */
	let status = findChild(container, '.ts-status-text');

	assert.ok(status, 'the status bar is missing');
	assert.ok(status.text.indexOf('Error') !== 0 && status.text.indexOf('not running') < 0,
		'the view reported a problem: ' + status.text);

	/* the live chart must contain the series */
	let charts = findAll(container, '.ts-chart');

	assert.ok(charts.length >= 2, 'the charts are missing');

	let live = findAll(charts[0], '.ts-line');

	assert.ok(live.length > 0, 'the live chart has no series');

	/* the history chart is drawn when the tab is opened */
	return Promise.resolve(instance.refreshHistory()).then(function() {
		let history = findAll(charts[1], '.ts-line');

		assert.ok(history.length > 0, 'the history chart has no series');

		console.log('view smoke test: OK (' + nodeCount + ' nodes created, status: ' +
			status.text + ', ' + live.length + ' live lines, ' + history.length + ' history lines)');
	});
}).catch(function(err) {
	console.error('view smoke test: FAILED');
	console.error(err);
	process.exit(1);
});

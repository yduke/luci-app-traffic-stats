/*
 * Small HTTP proxy that adds the LuCI session cookie to every request.
 * Used to load the LuCI page in a headless browser.
 *
 * Usage: node build/session-proxy.js <session-id> [port]
 */

'use strict';

const http = require('http');

const SID = process.argv[2];
const PORT = parseInt(process.argv[3] || '8099', 10);
const TARGET = '192.168.2.1';

if (!SID) {
	console.error('usage: node session-proxy.js <session-id> [port]');
	process.exit(1);
}

http.createServer((req, res) => {
	const headers = Object.assign({}, req.headers);

	headers.cookie = 'sysauth=' + SID + '; sysauth_http=' + SID + '; sysauth_https=' + SID;
	headers.host = TARGET;

	const proxy = http.request({
		hostname: TARGET,
		port: 80,
		path: req.url,
		method: req.method,
		headers: headers
	}, (upstream) => {
		res.writeHead(upstream.statusCode, upstream.headers);
		upstream.pipe(res);
	});

	proxy.on('error', (err) => {
		res.writeHead(502);
		res.end(String(err));
	});

	req.pipe(proxy);
}).listen(PORT, '127.0.0.1', () => {
	console.log('proxy on 127.0.0.1:' + PORT + ' -> ' + TARGET);
});

"use strict";
/*
 * Launcher-control bridge for the BeagleY-AI volleyball launcher.
 *
 * Connects the web app to the C control daemon (motor_control) over UDP.
 *
 *   browser --socket.io 'launcher-command' "cmd ..."--> here
 *          --UDP 127.0.0.1:12346--> motor_control control server
 *          <--UDP status/state/set replies-- motor_control
 *   browser <--socket.io 'launcher-reply' "status ..."-- here
 *
 * The control daemon binds to loopback only, so every component lives on the
 * same device. The command protocol is documented in
 * volleyball-launcher/app/src/include/control_server.h.
 */

var dgram = require('dgram');

// ---- Configuration (override via environment variables) -------------------
var LAUNCHER_HOST = process.env.LAUNCHER_HOST || '127.0.0.1';
var LAUNCHER_PORT = parseInt(process.env.LAUNCHER_PORT, 10) || 12346;

var MAX_COMMAND_LEN = 256;

var io;
var sock = null;

// Open the UDP socket and forward every launcher reply to all browsers.
exports.init = function(ioRef) {
	io = ioRef;
	sock = dgram.createSocket('udp4');

	sock.on('message', function(msg) {
		var line = msg.toString('utf8').trim();
		if (line && io) {
			io.sockets.emit('launcher-reply', line);
		}
	});

	sock.on('error', function(err) {
		console.log("Launcher control socket error: " + err.message);
	});

	// Bind to an ephemeral port so the daemon's replies land back on this socket.
	sock.bind(function() {
		console.log("Launcher control bridge ready (-> " +
				LAUNCHER_HOST + ":" + LAUNCHER_PORT + ")");
	});
};

// Register the per-browser command handler.
exports.handle = function(socket) {
	socket.on('launcher-command', function(line) {
		sendCommand(line, socket);
	});
};

function sendCommand(line, socket) {
	if (typeof line !== 'string') {
		return;
	}
	var trimmed = line.trim();

	// Only relay our own command protocol, and cap the length, so the bridge
	// cannot be coerced into sending arbitrary traffic to the daemon.
	if (trimmed !== 'cmd' && trimmed.slice(0, 4) !== 'cmd ') {
		if (socket) socket.emit('launcher-reply', 'status err rejected (must start with cmd)');
		return;
	}
	if (trimmed.length > MAX_COMMAND_LEN) {
		if (socket) socket.emit('launcher-reply', 'status err rejected (command too long)');
		return;
	}
	if (!sock) {
		if (socket) socket.emit('launcher-reply', 'status err launcher bridge not ready');
		return;
	}

	var buf = Buffer.from(trimmed);
	sock.send(buf, 0, buf.length, LAUNCHER_PORT, LAUNCHER_HOST, function(err) {
		if (err && socket) {
			socket.emit('launcher-reply', 'status err send failed: ' + err.message);
		}
	});
}

exports.shutdown = function() {
	if (sock) {
		try { sock.close(); } catch (e) { /* already closed */ }
		sock = null;
	}
};

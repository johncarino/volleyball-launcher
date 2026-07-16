"use strict";
/*
 * Gesture-control backend for the BeagleY-AI.
 *
 * Responsibilities:
 *   1. Control:  browser --socket.io--> here.  On "start" we spawn the MediaPipe
 *      recogniser (m2demo); on "stop" we kill it.
 *   2. Data:     m2demo --UDP 127.0.0.1:12345--> here.  Each datagram is a
 *      gesture summary line which we parse and broadcast to every browser as a
 *      "gesture-update" event.
 *
 * UDP wire protocol (one line per datagram, ASCII):
 *   "gesture <count> <thumb> <index> <middle> <ring> <pinky> <NAME>"
 *   e.g. "gesture 2 0 1 1 0 0 PEACE"
 */

var socketio = require('socket.io');
var dgram    = require('dgram');
var path     = require('path');
var spawn    = require('child_process').spawn;
var launcher = require('./launcher_control');

// ---- Configuration (override via environment variables) -------------------
var UDP_PORT  = parseInt(process.env.GESTURE_UDP_PORT, 10) || 12345;
var UDP_HOST  = process.env.GESTURE_UDP_HOST || '127.0.0.1';

// Path to the compiled MediaPipe recogniser binary and its graph config.
// On the BeagleY-AI these typically point into your cloned MediaPipe repo,
// e.g. <mediapipe>/bazel-bin/mediapipe/mediapipe_files/m2demo
var M2DEMO_BIN    = process.env.M2DEMO_BIN || 'm2demo';
var GRAPH_CONFIG  = process.env.GESTURE_GRAPH ||
        path.join(__dirname, '..', '..', 'mediapipe_files', 'hand_tracking_custom.pbtxt');
var CAMERA_INDEX  = process.env.GESTURE_CAMERA || '0';

// Optional low-latency MJPEG/RTP preview stream of the annotated frames.
// Set GESTURE_STREAM_HOST to a laptop's IP to watch what the camera sees;
// leave it unset to disable streaming (no extra cost on the board).
var STREAM_HOST   = process.env.GESTURE_STREAM_HOST || '';
var STREAM_PORT   = process.env.GESTURE_STREAM_PORT || '5000';

var io;
var recognizer = null;   // current child_process, or null

exports.listen = function(server) {
	io = socketio.listen(server);
	io.set('log level', 1);

	startUdpReceiver();
	launcher.init(io);

	io.sockets.on('connection', function(socket) {
		handleCommand(socket);
	});
};

// ---- Browser command handling ---------------------------------------------
function handleCommand(socket) {
	console.log("Browser connected; setting up gesture handlers.");

	socket.on('start', function() {
		console.log("Got start command.");
		startRecognizer(socket);
	});

	socket.on('stop', function() {
		console.log("Got stop command.");
		stopRecognizer(socket);
	});

	// Bridge launcher control commands (calibration, sets, operation, dev).
	launcher.handle(socket);

	// Report current state to a freshly-connected browser.
	socket.emit('state-reply', recognizer ? 'RUNNING' : 'IDLE');
}

function startRecognizer(socket) {
	if (recognizer) {
		socket.emit('start-reply', 'ALREADY_RUNNING');
		return;
	}

	var args = [
		'--calculator_graph_config_file=' + GRAPH_CONFIG,
		'--camera_index=' + CAMERA_INDEX,
		'--udp_host=' + UDP_HOST,
		'--udp_port=' + UDP_PORT
	];

	// Enable the live preview stream only when a destination host is configured.
	if (STREAM_HOST) {
		args.push('--stream_host=' + STREAM_HOST);
		args.push('--stream_port=' + STREAM_PORT);
	}

	console.log("Spawning recogniser: " + M2DEMO_BIN + " " + args.join(' '));

	try {
		recognizer = spawn(M2DEMO_BIN, args, {
			env: Object.assign({}, process.env, { GLOG_logtostderr: '1' })
		});
	} catch (e) {
		recognizer = null;
		socket.emit('gesture-error', "Failed to start recogniser: " + e.message);
		return;
	}

	recognizer.stdout.on('data', function(d) {
		console.log("[m2demo] " + d.toString().trim());
	});
	recognizer.stderr.on('data', function(d) {
		console.log("[m2demo] " + d.toString().trim());
	});
	recognizer.on('error', function(err) {
		console.log("Recogniser error: " + err.message);
		recognizer = null;
		if (io) io.sockets.emit('gesture-error',
				"Recogniser could not start (" + M2DEMO_BIN + "): " + err.message);
	});
	recognizer.on('exit', function(code, signal) {
		console.log("Recogniser exited (code=" + code + ", signal=" + signal + ").");
		recognizer = null;
		if (io) io.sockets.emit('recognizer-stopped', String(code === null ? signal : code));
	});

	socket.emit('start-reply', 'STARTED');
	if (io) io.sockets.emit('state-reply', 'RUNNING');
}

function stopRecognizer(socket) {
	if (!recognizer) {
		socket.emit('stop-reply', 'NOT_RUNNING');
		return;
	}
	recognizer.kill('SIGTERM');
	socket.emit('stop-reply', 'STOPPING');
}

// ---- UDP receiver (gesture summaries pushed by m2demo) ---------------------
function startUdpReceiver() {
	var receiver = dgram.createSocket('udp4');

	receiver.on('message', function(msg) {
		var data = parseGestureLine(msg.toString('utf8').trim());
		if (data && io) {
			io.sockets.emit('gesture-update', data);
		}
	});

	receiver.on('error', function(err) {
		console.log("UDP receiver error: " + err.message);
	});

	receiver.on('listening', function() {
		var a = receiver.address();
		console.log("Gesture UDP receiver listening on " + a.address + ":" + a.port);
	});

	receiver.bind(UDP_PORT, UDP_HOST);
}

// "gesture <count> <t> <i> <m> <r> <p> <NAME>" -> structured object, or null.
function parseGestureLine(line) {
	var parts = line.split(/\s+/);
	if (parts[0] !== 'gesture' || parts.length < 8) {
		return null;
	}
	return {
		count: parseInt(parts[1], 10) || 0,
		fingers: {
			thumb:  parts[2] === '1',
			index:  parts[3] === '1',
			middle: parts[4] === '1',
			ring:   parts[5] === '1',
			pinky:  parts[6] === '1'
		},
		name: parts[7]
	};
}

// ---- Clean up the child process when the server stops ----------------------
function shutdownRecognizer() {
	if (recognizer) {
		recognizer.kill('SIGTERM');
		recognizer = null;
	}
	launcher.shutdown();
}
process.on('exit', shutdownRecognizer);
process.on('SIGINT', function() { shutdownRecognizer(); process.exit(0); });
process.on('SIGTERM', function() { shutdownRecognizer(); process.exit(0); });

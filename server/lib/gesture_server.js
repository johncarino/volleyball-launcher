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

// ---- Configuration (override via environment variables) -------------------
var UDP_PORT  = parseInt(process.env.GESTURE_UDP_PORT, 10) || 12345;
var UDP_HOST  = process.env.GESTURE_UDP_HOST || '127.0.0.1';
var OPERATION_LAZY_INIT = process.env.OPERATION_LAZY_INIT === '1';

// Path to the compiled MediaPipe recogniser binary and its graph config.
// On the BeagleY-AI these typically point into your cloned MediaPipe repo,
// e.g. <mediapipe>/bazel-bin/mediapipe/mediapipe_files/m2demo
var M2DEMO_BIN    = process.env.M2DEMO_BIN || 'm2demo';
var GRAPH_CONFIG  = process.env.GESTURE_GRAPH ||
        path.join(__dirname, '..', '..', 'mediapipe_files', 'hand_tracking_custom.pbtxt');
var CAMERA_INDEX  = process.env.GESTURE_CAMERA || '0';

var io;
var recognizer = null;   // current child_process, or null
var operationInitAttempted = false;
var operationReady = false;

// ---- Native wrappers (built by: cd server && npm run build) --------------
var operation = (function() {
	try {
		var m = require('../build/Release/operation_wrapper');
		console.log('[operation] Native addon loaded.');
		return m;
	} catch (e) {
		console.warn('[operation] Native addon not available (' + e.message + '). Operation calls will be no-ops.');
		return {
			operationInit: function(){},
			operationCleanup: function(){},
			homingSequence: function(){},
			tiltSignal: function(){},
			speedSignal: function(){},
			resumeMachine: function(){},
			pauseMachine: function(){}
		};
	}
}());

var setApi = (function() {
	try {
		var m = require('../build/Release/set_wrapper');
		console.log('[set] Native addon loaded.');
		return m;
	} catch (e) {
		console.warn('[set] Native addon not available (' + e.message + '). Set calls will be no-ops.');
		return { saveSet: function(){} };
	}
}());

var calibration = (function() {
	try {
		var m = require('../build/Release/calibration_wrapper');
		console.log('[calibration] Native addon loaded.');
		return m;
	} catch (e) {
		console.warn('[calibration] Native addon not available (' + e.message + '). Calibration calls will be no-ops.');
		return {
			setNetHeight: function(){},
			setCourtDimensions: function(){},
			setCourtLength: function(){},
			setCourtWidth: function(){}
		};
	}
}());

exports.listen = function(server) {
	io = socketio.listen(server);
	io.set('log level', 1);

	startUdpReceiver();

	io.sockets.on('connection', function(socket) {
		handleCommand(socket);

		socket.on('advanced-enter', function() {
			initOperation(socket, 'advanced-enter');
		});

		socket.on('advanced-leave', function() {
			cleanupOperation(socket, 'advanced-leave');
		});
	});
};

function ensureOperationReady(socket, commandName) {
	if (!OPERATION_LAZY_INIT) {
		return true;
	}

	if (operationReady) {
		return true;
	}

	if (operationInitAttempted) {
		if (socket) socket.emit('machine-error', 'Operation mode is unavailable.');
		console.log('[operation] Command blocked (' + commandName + '): operation mode unavailable.');
		return false;
	}

	return initOperation(socket, commandName);
}

function initOperation(socket, reason) {
	if (operationReady) {
		if (socket) socket.emit('operation-state', 'READY');
		return true;
	}

	if (operationInitAttempted) {
		if (socket) socket.emit('operation-state', 'INITIALIZING');
		return false;
	}

	operationInitAttempted = true;
	try {
		operation.operationInit();
		operationReady = true;
		console.log('[operation] operationInit complete (' + reason + ').');
		if (socket) socket.emit('operation-state', 'READY');
		return true;
	} catch (e) {
		operationReady = false;
		console.log('[operation] operationInit failed: ' + e.message);
		if (socket) socket.emit('machine-error', 'Failed to initialize operation mode.');
		return false;
	}
}

function cleanupOperation(socket, reason) {
	if (!operationReady && !operationInitAttempted) {
		if (socket) socket.emit('operation-state', 'IDLE');
		return true;
	}

	try {
		if (operationReady) {
			operation.operationCleanup();
		}
		console.log('[operation] operationCleanup complete (' + reason + ').');
	} catch (e) {
		console.log('[operation] operationCleanup failed: ' + e.message);
	} finally {
		operationReady = false;
		operationInitAttempted = false;
		if (socket) socket.emit('operation-state', 'IDLE');
	}
	return true;
}

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

	socket.on('setSpeed', function(value) {
		if (!ensureOperationReady(socket, 'setSpeed')) return;
		console.log("Got setSpeed command: " + value);
		operation.speedSignal(value);
	});

	socket.on('setAngle', function(value) {
		if (!ensureOperationReady(socket, 'setAngle')) return;
		console.log("Got setAngle command: " + value);
		operation.tiltSignal(value);
	});

	socket.on('stopMotors', function() {
		if (!ensureOperationReady(socket, 'stopMotors')) return;
		console.log("Got stopMotors command.");
		operation.pauseMachine();
	});

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

	if (operationReady) {
		try {
			operation.operationCleanup();
			console.log('[operation] operationCleanup complete.');
		} catch (e) {
			console.log('[operation] operationCleanup failed: ' + e.message);
		}
		operationReady = false;
	}
}
process.on('exit', shutdownRecognizer);
process.on('SIGINT', function() { shutdownRecognizer(); process.exit(0); });
process.on('SIGTERM', function() { shutdownRecognizer(); process.exit(0); });

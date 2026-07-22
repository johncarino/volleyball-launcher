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
var fs       = require('fs');
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

// Bazel-built binaries resolve MediaPipe resource paths (e.g.
// "mediapipe/modules/palm_detection/palm_detection_full.tflite") relative to
// the process's working directory, expecting to be launched the way
// `bazel run` does: from inside the target's generated runfiles tree. Since
// we spawn the binary directly, point `cwd` at that runfiles directory
// (<M2DEMO_BIN>.runfiles/_main) so those relative lookups succeed. Override
// via M2DEMO_RUNFILES_DIR if your Bazel setup uses a different layout.
var M2DEMO_RUNFILES_DIR = process.env.M2DEMO_RUNFILES_DIR ||
        path.join(M2DEMO_BIN + '.runfiles', '_main');

var io;
var recognizer = null;   // current child_process, or null
var operationInitAttempted = false;
var operationReady = false;
var currentNetHeight = 2.43;
var currentCourtLength = 18.0;
var currentCourtWidth = 9.0;
// Track saved set data indexed by machinePosition and setIndex
var savedSets = {};  // savedSets[machinePos + '_' + setIdx] = { launch_speed, tilt_angle, yaw_angle, rpm_output, ... }

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
			getTachReading: function(){ return 0; },
			syncSet: function(){},
			setMachine: function(){},
			hopperStart: function(){},
			hopperStop: function(){},
			hopperPulse: function(){},
			resumeMachine: function(){},
			pauseMachine: function(){},
			requestInterrupt: function(){},
			isInterruptPending: function(){ return false; },
			clearInterrupt: function(){}
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
			setCourtWidth: function(){},
			defaultCalibration: function(){}
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
	if (operationReady) {
		return true;
	}

	if (OPERATION_LAZY_INIT && operationInitAttempted) {
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
		console.log('[operation] initOperation requested (' + reason + ').');
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

	// Software interrupt (emergency abort): asks any in-progress blocking
	// operation (tilt/speed feedback loops, hopper stepping, etc.) to abort
	// and leave the motors stopped. Wired to the UI's poweroff/quit button so
	// the hardware is halted before the server process exits.
	socket.on('requestInterrupt', function() {
		console.log("Got requestInterrupt command (emergency abort).");
		try {
			if (typeof operation.requestInterrupt === 'function') {
				operation.requestInterrupt();
			}
		} catch (e) {
			console.log('[operation] requestInterrupt failed: ' + e.message);
		}
	});

	// Allow the web UI to cleanly terminate the whole server process (in place
	// of Ctrl+C in the terminal). Request the software interrupt first so any
	// in-progress motor operation stops immediately, then run operation
	// cleanup right away (releasing the GPIO/I2C motor handles) rather than
	// waiting on the process 'exit' handler, then broadcast so every
	// connected browser (not just the one that clicked Quit) can show a
	// "shutting down" state, then exit.
	socket.on('quit-server', function() {
		console.log("Got quit-server command. Shutting down server...");
		try {
			if (typeof operation.requestInterrupt === 'function') {
				operation.requestInterrupt();
				console.log('[operation] software interrupt requested before shutdown.');
			}
		} catch (e) {
			console.log('[operation] requestInterrupt failed: ' + e.message);
		}
		cleanupOperation(null, 'quit-server');
		if (io) io.sockets.emit('server-shutdown', 'Server is shutting down...');
		setTimeout(function() {
			process.exit(0);
		}, 250);
	});

	socket.on('page-loaded', function() {
		console.log("Got page-loaded event. Applying default calibration.");
		try {
			calibration.defaultCalibration();
			currentNetHeight = 2.43;
			currentCourtLength = 18.0;
			currentCourtWidth = 9.0;
			if (typeof setApi.setCalibration === 'function') {
				setApi.setCalibration(currentNetHeight, currentCourtLength, currentCourtWidth);
			}
			socket.emit('calibration-state', 'DEFAULT_APPLIED');
		} catch (e) {
			console.log('[calibration] defaultCalibration failed: ' + e.message);
			socket.emit('machine-error', 'Failed to apply default calibration.');
		}
	});

	socket.on('setNetHeight', function(value) {
		console.log("Got setNetHeight command: " + value);
		try {
			var netHeight = parseFloat(value);
			calibration.setNetHeight(netHeight);
			currentNetHeight = netHeight;
			if (typeof setApi.setCalibration === 'function') {
				setApi.setCalibration(currentNetHeight, currentCourtLength, currentCourtWidth);
			}
		} catch (e) {
			console.log('[calibration] setNetHeight failed: ' + e.message);
			socket.emit('machine-error', 'Failed to set net height.');
		}
	});

	socket.on('setCourtLength', function(value) {
		console.log("Got setCourtLength command: " + value);
		try {
			var courtLength = parseFloat(value);
			calibration.setCourtLength(courtLength);
			currentCourtLength = courtLength;
			if (typeof setApi.setCalibration === 'function') {
				setApi.setCalibration(currentNetHeight, currentCourtLength, currentCourtWidth);
			}
		} catch (e) {
			console.log('[calibration] setCourtLength failed: ' + e.message);
			socket.emit('machine-error', 'Failed to set court length.');
		}
	});

	socket.on('setCourtWidth', function(value) {
		console.log("Got setCourtWidth command: " + value);
		try {
			var courtWidth = parseFloat(value);
			calibration.setCourtWidth(courtWidth);
			currentCourtWidth = courtWidth;
			if (typeof setApi.setCalibration === 'function') {
				setApi.setCalibration(currentNetHeight, currentCourtLength, currentCourtWidth);
			}
		} catch (e) {
			console.log('[calibration] setCourtWidth failed: ' + e.message);
			socket.emit('machine-error', 'Failed to set court width.');
		}
	});

	socket.on('saveSet', function(payload) {
		console.log('Got saveSet command:', payload);
		try {
			if (!payload || typeof payload !== 'object') {
				throw new Error('Invalid payload');
			}

			var setIndex = parseInt(payload.setIndex, 10);
			var machinePosition = parseInt(payload.machinePosition, 10);
			var targetLocation = parseInt(payload.targetLocation, 10);
			var tempo = parseInt(payload.tempo, 10);

			if (Number.isNaN(setIndex) || Number.isNaN(machinePosition) ||
				Number.isNaN(targetLocation) || Number.isNaN(tempo)) {
				throw new Error('saveSet payload contains non-numeric fields');
			}

			if (typeof setApi.setCalibration === 'function') {
				setApi.setCalibration(currentNetHeight, currentCourtLength, currentCourtWidth);
			}

			// saveSet now returns the saved set data or null if save failed
			var setData = setApi.saveSet(setIndex, machinePosition, targetLocation, tempo);
			
			if (setData) {
				// Store the set data for later use by setMachine
				var key = machinePosition + '_' + setIndex;
				savedSets[key] = setData;
				socket.emit('set-save-state', 'SAVED');
			} else {
				throw new Error('saveSet returned no data (validation may have failed)');
			}
		} catch (e) {
			console.log('[set] saveSet failed: ' + e.message);
			socket.emit('machine-error', 'Failed to save set slot.');
		}
	});

	socket.on('setMachine', function(payload) {
		console.log('Got setMachine command:', payload);
		try {
			if (!payload || typeof payload !== 'object') {
				throw new Error('Invalid payload');
			}

			var setIndex = parseInt(payload.setIndex, 10);
			var machinePosition = parseInt(payload.machinePosition, 10);

			if (Number.isNaN(setIndex) || Number.isNaN(machinePosition)) {
				throw new Error('setMachine payload contains non-numeric fields');
			}

			if (typeof operation.syncSet !== 'function') {
				throw new Error('syncSet is not available in the operation addon');
			}

			if (typeof operation.setMachine !== 'function') {
				throw new Error('setMachine is not available in the operation addon');
			}

			if (!ensureOperationReady(socket, 'setMachine')) return;
			
			// Look up the saved set data
			var key = machinePosition + '_' + setIndex;
			var setData = savedSets[key];
			
			if (!setData) {
				throw new Error('No saved set data for machine ' + machinePosition + ' set ' + setIndex);
			}
			
			// Sync the set data to operation_wrapper before calling setMachine
			operation.syncSet(
				machinePosition,
				setIndex,
				setData.launch_speed,
				setData.tilt_angle,
				setData.yaw_angle,
				setData.rpm_output,
				setData.target_location,
				setData.tempo
			);
			
			// Now call setMachine to apply it
			operation.setMachine(machinePosition, setIndex);
			socket.emit('set-machine-state', 'APPLIED');
		} catch (e) {
			console.log('[operation] setMachine failed: ' + e.message);
			socket.emit('machine-error', 'Failed to apply set machine.');
		}
	});

	socket.on('setSpeed', function(value) {
		if (!ensureOperationReady(socket, 'setSpeed')) return;
		console.log("Got setSpeed command: " + value);
		operation.speedSignal(value);
	});

	socket.on('setAngle', function(value) {
		if (!ensureOperationReady(socket, 'setAngle')) return;
		console.log("Got setAngle command: " + value);
		console.log('[operation] forwarding setAngle to native tiltSignal.');
		operation.tiltSignal(value);
	});

	socket.on('stopMotors', function() {
		if (!ensureOperationReady(socket, 'stopMotors')) return;
		console.log("Got stopMotors command.");
		operation.pauseMachine();
	});

	socket.on('resumeMotors', function() {
		if (!ensureOperationReady(socket, 'resumeMotors')) return;
		console.log("Got resumeMotors command.");
		operation.resumeMachine();
	});

	socket.on('hopper-on', function() {
		if (!ensureOperationReady(socket, 'hopper-on')) return;
		console.log("Got hopper-on command.");
		operation.hopperStart();
	});

	socket.on('hopper-off', function() {
		if (!ensureOperationReady(socket, 'hopper-off')) return;
		console.log("Got hopper-off command.");
		operation.hopperStop();
	});

	socket.on('hopper-pulse', function() {
		if (!ensureOperationReady(socket, 'hopper-pulse')) return;
		console.log("Got hopper-pulse command.");
		operation.hopperPulse();
	});

	socket.on('requestTelemetry', function() {
		if (!ensureOperationReady(socket, 'requestTelemetry')) return;
		try {
			var rpm = 0;
			if (typeof operation.getTachReading === 'function') {
				rpm = parseInt(operation.getTachReading(), 10) || 0;
			}
			socket.emit('telemetry-update', { rpm: rpm });
		} catch (e) {
			console.log('[operation] requestTelemetry failed: ' + e.message);
			socket.emit('machine-error', 'Failed to get telemetry.');
		}
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

	var spawnOptions = {
		env: Object.assign({}, process.env, { GLOG_logtostderr: '1' })
	};
	if (fs.existsSync(M2DEMO_RUNFILES_DIR)) {
		spawnOptions.cwd = M2DEMO_RUNFILES_DIR;
	} else {
		console.log("[m2demo] Runfiles dir not found (" + M2DEMO_RUNFILES_DIR + "); " +
				"resource loading may fail if the binary needs it.");
	}

	console.log("Spawning recogniser: " + M2DEMO_BIN + " " + args.join(' ') +
			(spawnOptions.cwd ? " (cwd=" + spawnOptions.cwd + ")" : ""));

	try {
		recognizer = spawn(M2DEMO_BIN, args, spawnOptions);
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
	try {
		if (typeof operation.requestInterrupt === 'function') {
			operation.requestInterrupt();
		}
	} catch (e) {
		console.log('[operation] requestInterrupt failed during shutdown: ' + e.message);
	}

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

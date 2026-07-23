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
var execSync = require('child_process').execSync;

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
// Stable USB camera selection. /dev/videoN numbers get reshuffled across reboots
// (e.g. a platform JPEG encoder can take video0), so we prefer the persistent
// /dev/v4l/by-id symlink keyed by the camera's USB id. GESTURE_CAMERA_DEVICE
// pins an explicit path; GESTURE_CAMERA_NAME filters by-id entries (e.g. 'C920').
var CAMERA_DEVICE = process.env.GESTURE_CAMERA_DEVICE || '';
var CAMERA_NAME   = process.env.GESTURE_CAMERA_NAME || '';
// Camera kind: 'usb' (UVC webcam like the Logitech HD Pro Webcam C920, which
// has its own ISP so no software debayer/WB/AE is needed; default) or 'csi'
// (BeagleY-AI IMX219 raw-Bayer path). USB also drops the 180 flip (mount it
// upright). Override with GESTURE_CAMERA_KIND=csi to use the IMX219.
var CAMERA_KIND   = (process.env.GESTURE_CAMERA_KIND || 'usb').toLowerCase();
var USB_FOCUS     = process.env.GESTURE_USB_FOCUS || '-1';
// Which hand signs sets; the other hand starts/stops the feed. Toggleable live
// from the web UI (also corrects MediaPipe handedness if it labels your hands
// the opposite way). 'right' (default) or 'left'.
var SET_HAND      = (process.env.GESTURE_SET_HAND || 'right').toLowerCase();
if (SET_HAND !== 'left') SET_HAND = 'right';
// Live preview: m2demo JPEG-encodes throttled frames and sends them to this
// local UDP port; we relay each frame to the browser over socket.io. Disabled
// when GESTURE_PREVIEW=0 or the port is 0. Kept modest (fps/quality) so it
// barely touches the recogniser's budget.
var PREVIEW_ENABLED = process.env.GESTURE_PREVIEW !== '0';
var PREVIEW_PORT    = parseInt(process.env.GESTURE_PREVIEW_PORT || '12346', 10);
var PREVIEW_FPS     = process.env.GESTURE_PREVIEW_FPS || '12';
var PREVIEW_QUALITY = process.env.GESTURE_PREVIEW_QUALITY || '45';

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
	startPreviewReceiver();

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

	// Opt-in gesture-control mode: while enabled, recognised gestures drive the
	// launcher (set selection + start/stop) instead of only updating the display.
	socket.on('gesture-control-enable', function(payload) {
		gestureControl.enabled = !!(payload && payload.enabled);
		if (payload && payload.machinePosition != null) {
			var mp = parseInt(payload.machinePosition, 10);
			if (!Number.isNaN(mp)) gestureControl.machinePosition = mp;
		}
		// Reset debounce state whenever the mode or position changes.
		resetGestureHolds();
		console.log('[gesture] control ' + (gestureControl.enabled ? 'ENABLED' : 'disabled') +
				' (machinePosition=' + gestureControl.machinePosition + ')');
		socket.emit('gesture-control-state', {
			enabled: gestureControl.enabled,
			machinePosition: gestureControl.machinePosition,
			feasible: FEASIBILITY[gestureControl.machinePosition] || []
		});
	});

	// Choose which hand signs sets ('right'/'left'); the other hand starts/stops.
	// Applied live (no recogniser restart) and echoed back so every browser stays
	// in sync.
	socket.on('gesture-set-hand', function(payload) {
		SET_HAND = (payload && payload.hand === 'left') ? 'left' : 'right';
		resetGestureHolds();
		console.log('[gesture] set-sign hand = ' + SET_HAND);
		if (io) io.sockets.emit('gesture-set-hand-state', { hand: SET_HAND });
	});

	// Tell the newly-connected browser the current signing-hand choice.
	socket.emit('gesture-set-hand-state', { hand: SET_HAND });

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

// Resolve a stable USB camera device node. /dev/videoN can be reassigned on
// reboot, and a UVC camera like the C920 exposes two nodes (capture + metadata),
// so we prefer the persistent /dev/v4l/by-id symlink and pick the capture node
// (…-video-index0). Precedence: explicit GESTURE_CAMERA_DEVICE override, then a
// by-id match, then a v4l2-ctl name lookup, else null (caller falls back to the
// numeric --camera_index). All lookups are (optionally) filtered by
// GESTURE_CAMERA_NAME (e.g. 'C920').
function resolveUsbCameraPath() {
	if (CAMERA_DEVICE) return CAMERA_DEVICE;
	return findByIdCapture() || findByV4l2List() || null;
}

// Persistent /dev/v4l/by-id capture symlink (…-video-index0). Best option: the
// path itself never changes across reboots.
function findByIdCapture() {
	var dir = '/dev/v4l/by-id';
	try {
		var entries = fs.readdirSync(dir).filter(function(n) {
			return /-video-index0$/.test(n) &&
				(!CAMERA_NAME || n.toLowerCase().indexOf(CAMERA_NAME.toLowerCase()) !== -1);
		});
		if (entries.length > 0) {
			entries.sort();
			return dir + '/' + entries[0];
		}
	} catch (e) {
		// by-id directory missing (no udev symlinks); fall through.
	}
	return null;
}

// Fallback: parse `v4l2-ctl --list-devices` and take the first video node of the
// USB camera group (that first node is the capture node; later ones are
// metadata). Re-resolved on every start, so it tracks renumbering even without
// by-id symlinks.
function findByV4l2List() {
	var out;
	try {
		out = execSync('v4l2-ctl --list-devices 2>/dev/null', { encoding: 'utf8' });
	} catch (e) {
		return null;
	}
	var want = (CAMERA_NAME || '').toLowerCase();
	var groups = out.split(/\n(?=\S)/);  // each group header starts at column 0
	for (var i = 0; i < groups.length; i++) {
		var lines = groups[i].split('\n');
		var header = (lines[0] || '').toLowerCase();
		var node = null;
		for (var j = 1; j < lines.length; j++) {
			var m = lines[j].match(/\/dev\/video\d+/);
			if (m) { node = m[0]; break; }
		}
		if (!node) continue;
		var matches = want ? header.indexOf(want) !== -1 : header.indexOf('usb') !== -1;
		if (matches) return node;
	}
	return null;
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

	if (CAMERA_KIND === 'usb') {
		// UVC webcam (e.g. Logitech C920): skip the CSI raw-Bayer/GStreamer path
		// and the 180 flip, and lock autofocus so it doesn't hunt on a moving hand.
		args.push('--use_gstreamer=false');
		args.push('--rotate180=false');
		args.push('--usb_disable_autofocus=true');
		args.push('--usb_focus=' + USB_FOCUS);
		// Mirror the (non-selfie) webcam so MediaPipe handedness matches the real
		// hand and the preview reads like a mirror. Override with GESTURE_MIRROR=0.
		args.push('--mirror=' + (process.env.GESTURE_MIRROR === '0' ? 'false' : 'true'));
		// Prefer a stable device path so a reboot renumbering /dev/videoN (or the
		// C920's second, metadata-only node) doesn't break capture.
		var camPath = resolveUsbCameraPath();
		if (camPath) {
			args.push('--camera_path=' + camPath);
			console.log('[gesture] USB camera device: ' + camPath);
		} else {
			console.log('[gesture] No stable by-id camera node found; falling back to ' +
					'/dev/video' + CAMERA_INDEX + '. Set GESTURE_CAMERA_DEVICE to pin it.');
		}
	}

	if (PREVIEW_ENABLED && PREVIEW_PORT > 0) {
		// Push throttled JPEG frames to our local preview receiver for the web UI.
		args.push('--preview_udp_host=' + UDP_HOST);
		args.push('--preview_udp_port=' + PREVIEW_PORT);
		args.push('--preview_fps=' + PREVIEW_FPS);
		args.push('--preview_quality=' + PREVIEW_QUALITY);
	}

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
		var hands = parseHandsLine(msg.toString('utf8').trim());
		if (!hands || !io) return;
		var roles = assignHandRoles(hands);
		var signData = roles.sign || NONE_HAND;
		io.sockets.emit('gesture-update', signData);
		handleSignFrame(signData);
		handleControlFrame(roles.control || NONE_HAND);
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

// ---- UDP preview receiver (JPEG frames pushed by m2demo) ------------------
// Each datagram is one whole JPEG frame (kept under the loopback MTU by the
// recogniser). We relay it to the browser as binary; socket.io drops it
// cheaply when nobody is connected, and m2demo only sends while running.
function startPreviewReceiver() {
	if (!PREVIEW_ENABLED || !(PREVIEW_PORT > 0)) return;
	var receiver = dgram.createSocket('udp4');

	receiver.on('message', function(msg) {
		if (io) io.sockets.emit('gesture-frame', msg);
	});

	receiver.on('error', function(err) {
		console.log("Preview UDP receiver error: " + err.message);
	});

	receiver.on('listening', function() {
		var a = receiver.address();
		console.log("Gesture preview UDP receiver listening on " + a.address + ":" + a.port);
	});

	receiver.bind(PREVIEW_PORT, UDP_HOST);
}

// A "no hand in this role" placeholder matching the gesture-update shape.
var NONE_HAND = {
	count: 0,
	fingers: { thumb: false, index: false, middle: false, ring: false, pinky: false },
	name: 'NONE',
	hand: 'none'
};

// "hands <n> [<label> <count> <t> <i> <m> <r> <p> <NAME>]..." -> array of hand
// objects, or null. <label> is R/L/U (right/left/unknown).
function parseHandsLine(line) {
	var parts = line.split(/\s+/);
	if (parts[0] !== 'hands') return null;
	var n = parseInt(parts[1], 10);
	if (Number.isNaN(n)) return null;
	var hands = [];
	var idx = 2;
	for (var k = 0; k < n; k++) {
		if (idx + 8 > parts.length) break;  // malformed / truncated
		var label = parts[idx];
		hands.push({
			hand: label === 'R' ? 'right' : (label === 'L' ? 'left' : 'unknown'),
			count: parseInt(parts[idx + 1], 10) || 0,
			fingers: {
				thumb:  parts[idx + 2] === '1',
				index:  parts[idx + 3] === '1',
				middle: parts[idx + 4] === '1',
				ring:   parts[idx + 5] === '1',
				pinky:  parts[idx + 6] === '1'
			},
			name: parts[idx + 7]
		});
		idx += 8;
	}
	return hands;
}

// Split the visible hands into the set-signing hand and the start/stop control
// hand according to the SET_HAND preference. Falls back to treating a lone
// unlabelled hand as the signing hand so single-hand use still works.
function assignHandRoles(hands) {
	var signWant = SET_HAND;
	var ctrlWant = (SET_HAND === 'right') ? 'left' : 'right';
	var sign = null, control = null;
	for (var i = 0; i < hands.length; i++) {
		if (hands[i].hand === signWant && !sign) sign = hands[i];
		else if (hands[i].hand === ctrlWant && !control) control = hands[i];
	}
	if (!sign && hands.length === 1 && hands[0].hand === 'unknown') {
		sign = hands[0];
	}
	return { sign: sign, control: control };
}

// ---- Gesture -> launcher intent mapping (gesture-control mode) -------------
// The detector emits generic hand-shape names; here we translate the ones we
// care about into volleyball set labels. THUMBS_UP is a control gesture, and
// OPEN_PALM is dual-purpose (brief hold = the "5" set, long hold = STOP).
var GESTURE_SET_MAP = {
	POINT:     '1',
	PEACE:     '2',
	FOUR:      '4',
	OPEN_PALM: '5',
	FIST:      'Red',
	CALL_ME:   'Slide',
	GUN:       '3/Shoot'
};

// Which sets are reachable from each machine position (0=Left,1=Center,2=Right).
// PLACEHOLDER: until the RPM/angle model exists these are best-guess
// reachability lists; edit freely. A set not listed is shown greyed-out and its
// gesture is ignored.
var FEASIBILITY = {
	0: ['1', '2', '4', 'Slide', '3/Shoot'],              // Left
	1: ['1', '2', '4', '5', 'Red', 'Slide', '3/Shoot'], // Center
	2: ['1', '2', '5', 'Red', 'Slide']                  // Right
};

// Debounce / hold thresholds (ms).
var GESTURE_HOLD_MS   = 700;   // hold a signal this long to confirm a set
var OPEN_PALM_STOP_MS = 1500;  // continuous OPEN_PALM this long = STOP

// Gesture-control runtime state (opt-in via 'gesture-control-enable'). The
// signing hand and the control hand are tracked independently so a set sign on
// one hand and a start/stop gesture on the other don't clobber each other.
var gestureControl = {
	enabled: false,
	machinePosition: 1,  // default Center
	sign:    { lastName: null, holdSince: 0, firedName: null },
	control: { lastName: null, holdSince: 0, firedName: null }
};

function resetGestureHolds() {
	gestureControl.sign =    { lastName: null, holdSince: 0, firedName: null };
	gestureControl.control = { lastName: null, holdSince: 0, firedName: null };
}

// Apply set-selection logic to the signing hand. Only acts when the browser has
// enabled gesture control; otherwise gestures are display-only. Start/stop is
// handled separately by the control hand (see handleControlFrame).
function handleSignFrame(data) {
	if (!gestureControl.enabled) return;

	var st = gestureControl.sign;
	var name = (data && data.name) ? data.name : 'NONE';
	var now = Date.now();

	if (name !== st.lastName) {
		st.lastName = name;
		st.holdSince = now;
		st.firedName = null;
	}
	var heldMs = now - st.holdSince;

	if (name === 'NONE' || name === 'UNKNOWN') return;
	if (heldMs < GESTURE_HOLD_MS) return;          // not held long enough yet
	if (st.firedName === name) return;             // already acted on this hold

	var set = GESTURE_SET_MAP[name];
	if (!set) return;  // not a mapped set gesture

	st.firedName = name;
	var allowed = FEASIBILITY[gestureControl.machinePosition] || [];
	var feasible = allowed.indexOf(set) !== -1;
	if (io) io.sockets.emit('gesture-set', { name: name, set: set, feasible: feasible });
	console.log('[gesture] set ' + set + ' (' + name + ') feasible=' + feasible +
			' mp=' + gestureControl.machinePosition);
	// NOTE: motor set-actuation (operation.setMachine) is intentionally NOT wired
	// yet -- the RPM/angle mapping is not implemented. Display only for now.
}

// Apply start/stop logic to the control hand (the one not signing sets):
// THUMBS_UP = start the feed, a long OPEN_PALM hold = stop.
function handleControlFrame(data) {
	if (!gestureControl.enabled) return;

	var st = gestureControl.control;
	var name = (data && data.name) ? data.name : 'NONE';
	var now = Date.now();

	if (name !== st.lastName) {
		st.lastName = name;
		st.holdSince = now;
		st.firedName = null;
	}
	var heldMs = now - st.holdSince;

	if (name === 'NONE' || name === 'UNKNOWN') return;

	// OPEN_PALM long continuous hold escalates to STOP (checked before the
	// once-per-hold guard).
	if (name === 'OPEN_PALM' && heldMs >= OPEN_PALM_STOP_MS) {
		if (st.firedName !== 'STOP') {
			st.firedName = 'STOP';
			triggerControl('stop');
		}
		return;
	}

	if (heldMs < GESTURE_HOLD_MS) return;          // not held long enough yet
	if (st.firedName === name) return;             // already acted on this hold

	// THUMBS_UP is the start control gesture.
	if (name === 'THUMBS_UP') {
		st.firedName = name;
		triggerControl('start');
	}
}

// Start/stop the ball feed from a control gesture. Wired to the existing hopper
// controls; guarded so it no-ops if operation mode can't initialise.
function triggerControl(action) {
	try {
		if (action === 'start') {
			if (!ensureOperationReady(null, 'gesture-start')) return;
			operation.hopperStart();
		} else {
			operation.hopperStop();
		}
		if (io) io.sockets.emit('gesture-control-action', { action: action });
		console.log('[gesture] control action: ' + action);
	} catch (e) {
		console.log('[gesture] control action failed: ' + e.message);
	}
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

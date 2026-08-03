"use strict";
/*
 * Gesture-control backend for the BeagleY-AI.
 *
 * Responsibilities:
 *   1. Control:  browser --socket.io--> here.  On "start" we spawn the MediaPipe
 *      recogniser (m2demo); on "stop" we kill it.
 *   2. Data:     m2demo --UDP 127.0.0.1:12345--> here.  Each datagram is a
 *      pose summary line which we parse and broadcast to every browser as a
 *      "gesture-update" event shaped { present, armRaised }. There is no
 *      gesture->set mapping here; the browser itself watches for a raised arm
 *      held ~3s to start the Single Shot / Sequence run (see
 *      armLaunchGesture/handleLaunchGestureFrame in public/index.html).
 *
 * UDP wire protocol (one line per datagram, ASCII):
 *   "pose <present> <arm_raised>"  where each field is 0/1, e.g. "pose 1 1"
 *   (person visible, an arm raised) or "pose 0 0" (no person).
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
        path.join(__dirname, '..', '..', 'mediapipe_files', 'pose_tracking_custom.pbtxt');
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

// ---- Shared, server-authoritative UI state (cross-device sync) -------------
// The browser used to keep its saved slots / enable flags / calibration only in
// its own tab, so a second device never saw the first one's changes. We now
// mirror the committed config here and broadcast every change to all clients, so
// a freshly connected device can be brought up to date with buildStateSnapshot.
var slotDescriptors = {};   // key -> { machinePosition, setIndex, targetLocation, tempo }
var slotEnabledState = {};  // key -> bool (whether the slot is armed for the run queue)
var sharedUiState = null;   // last live selections/options from any client (see 'uiState')
var calibrationInitialized = false;  // guards page-loaded from resetting a peer's calibration
var hasHomedOnStartup = false;       // one-time homing after the machine/server starts

function buildStateSnapshot() {
	var slots = Object.keys(slotDescriptors).map(function(key) {
		var d = slotDescriptors[key];
		return {
			machinePosition: d.machinePosition,
			setIndex: d.setIndex,
			targetLocation: d.targetLocation,
			tempo: d.tempo,
			enabled: !!slotEnabledState[key]
		};
	});
	return {
		slots: slots,
		calibration: {
			net: Math.round(currentNetHeight * 100),
			len: currentCourtLength,
			wid: currentCourtWidth
		},
		uiState: sharedUiState
	};
}

function broadcastCalibration() {
	if (!io) return;
	io.sockets.emit('calibration-updated', {
		net: Math.round(currentNetHeight * 100),
		len: currentCourtLength,
		wid: currentCourtWidth
	});
}

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
			tiltSignal: function(angle, callback){
				setImmediate(function() { callback(null, true); });
				return true;
			},
			speedSignal: function(){},
			getTachReading: function(){ return 0; },
			getHopperPulseCount: function(){ return 0; },
			getComponentStatus: function(){ return 0; },
			syncSet: function(){},
			setMachine: function(machinePosition, setIndex, callback){
				setImmediate(function() { callback(null, true); });
				return true;
			},
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
		return { saveSet: function(){}, recalculateSets: function(){ return []; } };
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

	// Home once when the machine/server starts. When operation control isn't
	// lazily initialised, bring it up now so the one-time homing runs at boot;
	// otherwise it runs the first time a client enters an operation mode.
	if (!OPERATION_LAZY_INIT) {
		initOperation(null, 'startup');
	}

	io.sockets.on('connection', function(socket) {
		handleCommand(socket);

		// Bring a freshly connected device up to date with the shared config.
		socket.emit('state-snapshot', buildStateSnapshot());

		socket.on('advanced-enter', function() {
			initOperation(socket, 'advanced-enter');
			startTelemetry(socket);
			emitComponentStatus(socket);
		});

		socket.on('advanced-leave', function() {
			stopTelemetry(socket);
			cleanupOperation(socket, 'advanced-leave');
		});

		socket.on('operation-enter', function() {
			initOperation(socket, 'operation-enter');
		});

		socket.on('requestComponentStatus', function() {
			emitComponentStatus(socket);
		});

		socket.on('operation-leave', function() {
			cleanupOperation(socket, 'operation-leave');
		});

		socket.on('disconnect', function() {
			stopTelemetry(socket);
		});
	});
};

// ---- Live telemetry (real tachometer RPM pushed to the Manual tab) ---------
var TELEMETRY_INTERVAL_MS = 500;

function startTelemetry(socket) {
	if (!socket || socket._telemetryTimer) return;
	socket._telemetryTimer = setInterval(function() {
		if (!operationReady) return;
		try {
			var rpm = operation.getTachReading();
			socket.emit('telemetry', { rpm: rpm });
		} catch (e) {
			// Transient read errors are ignored; next tick will retry.
		}
	}, TELEMETRY_INTERVAL_MS);
}

function stopTelemetry(socket) {
	if (socket && socket._telemetryTimer) {
		clearInterval(socket._telemetryTimer);
		socket._telemetryTimer = null;
	}
}

// Push per-component init status (from operation_component_status()) so the
// Manual page can show which hardware actually came up.
function emitComponentStatus(socket) {
	if (!socket) return;
	var mask = 0;
	try { mask = operation.getComponentStatus(); } catch (e) { mask = 0; }
	socket.emit('component-status', {
		ready: operationReady,
		tachometer:    !!(mask & 1),
		imu:           !!(mask & 2),
		tiltDriver:    !!(mask & 4),
		hopperStepper: !!(mask & 8),
		flywheelDac:   !!(mask & 16),
		ballSensor:    !!(mask & 32)
	});
}

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
		maybeStartupHoming();
		return true;
	} catch (e) {
		operationReady = false;
		console.log('[operation] operationInit failed: ' + e.message);
		if (socket) socket.emit('machine-error', 'Failed to initialize operation mode.');
		return false;
	}
}

// Run the homing sequence exactly once, the first time motor control becomes
// available after the machine/server starts. Switching UI tabs no longer homes
// (that used to happen on every Configure/Manual/Launch switch); the manual
// "Home machine" button remains for on-demand re-homing.
function maybeStartupHoming() {
	if (hasHomedOnStartup || !operationReady) return;
	hasHomedOnStartup = true;
	try {
		console.log('[operation] Running one-time startup homing sequence.');
		operation.homingSequence();
		if (io) io.sockets.emit('homing-complete');
	} catch (e) {
		console.error('[operation] Startup homing failed: ' + e.message);
		if (io) io.sockets.emit('homing-error', 'Startup homing failed.');
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
		console.log("Got page-loaded event.");
		try {
			// Only apply the factory defaults the first time — after that the
			// shared calibration belongs to whoever set it, so a second device
			// loading the page must not wipe it. Either way, hand the client the
			// current shared state so it renders what everyone else sees.
			if (!calibrationInitialized) {
				calibration.defaultCalibration();
				currentNetHeight = 2.43;
				currentCourtLength = 18.0;
				currentCourtWidth = 9.0;
				if (typeof setApi.setCalibration === 'function') {
					setApi.setCalibration(currentNetHeight, currentCourtLength, currentCourtWidth);
				}
				calibrationInitialized = true;
				socket.emit('calibration-state', 'DEFAULT_APPLIED');
			}
			socket.emit('state-snapshot', buildStateSnapshot());
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
			calibrationInitialized = true;
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
			calibrationInitialized = true;
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
			calibrationInitialized = true;
			if (typeof setApi.setCalibration === 'function') {
				setApi.setCalibration(currentNetHeight, currentCourtLength, currentCourtWidth);
			}
		} catch (e) {
			console.log('[calibration] setCourtWidth failed: ' + e.message);
			socket.emit('machine-error', 'Failed to set court width.');
		}
	});

	// Sent once the user leaves the court settings tab: re-applies the
	// (possibly just-changed) calibration and re-derives every already-saved
	// set slot from it, since save_set() only snapshots values at save time
	// and won't otherwise pick up a later calibration change.
	socket.on('recalculateSets', function() {
		console.log("Got recalculateSets command (court settings changed).");
		try {
			if (typeof setApi.setCalibration === 'function') {
				setApi.setCalibration(currentNetHeight, currentCourtLength, currentCourtWidth);
			}
			var updatedSlots = (typeof setApi.recalculateSets === 'function') ? setApi.recalculateSets() : [];

			// recalculateSets() only touches the native set_seq array directly;
			// refresh the JS-side savedSets cache (used by setMachine) to match,
			// since it was populated from saveSet()'s return value at save time
			// and won't otherwise see this update.
			if (Array.isArray(updatedSlots)) {
				updatedSlots.forEach(function(slot) {
					var key = slot.machine_position + '_' + slot.set_index;
					savedSets[key] = slot;
				});
			}

			console.log('[set] Recalculated ' + (Array.isArray(updatedSlots) ? updatedSlots.length : 0) + ' saved set slot(s) for new calibration.');
			socket.emit('sets-recalculated', { updated: Array.isArray(updatedSlots) ? updatedSlots.length : 0 });
			// Mirror the new calibration to every other device's court summary.
			broadcastCalibration();
		} catch (e) {
			console.log('[set] recalculateSets failed: ' + e.message);
			socket.emit('machine-error', 'Failed to recalculate saved sets.');
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
				slotDescriptors[key] = {
					machinePosition: machinePosition,
					setIndex: setIndex,
					targetLocation: targetLocation,
					tempo: tempo
				};
				slotEnabledState[key] = true;
				socket.emit('set-save-state', 'SAVED');
				// Mirror the new slot to every other connected device.
				if (io) io.sockets.emit('set-saved', {
					machinePosition: machinePosition,
					setIndex: setIndex,
					targetLocation: targetLocation,
					tempo: tempo,
					enabled: true
				});
			} else {
				throw new Error('saveSet returned no data (validation may have failed)');
			}
		} catch (e) {
			console.log('[set] saveSet failed: ' + e.message);
			socket.emit('machine-error', 'Failed to save set slot.');
		}
	});

	// Enable/disable a saved slot for the sequence run queue (broadcast so every
	// device shows the same armed slots).
	socket.on('setSlotEnabled', function(payload) {
		try {
			if (!payload || typeof payload !== 'object') return;
			var machinePosition = parseInt(payload.machinePosition, 10);
			var setIndex = parseInt(payload.setIndex, 10);
			var enabled = payload.enabled === true;
			if (Number.isNaN(machinePosition) || Number.isNaN(setIndex)) return;
			var key = machinePosition + '_' + setIndex;
			if (!slotDescriptors[key]) return;
			slotEnabledState[key] = enabled;
			if (io) io.sockets.emit('slot-enabled', {
				machinePosition: machinePosition,
				setIndex: setIndex,
				enabled: enabled
			});
		} catch (e) {
			console.log('[set] setSlotEnabled failed: ' + e.message);
		}
	});

	// Delete a saved slot (broadcast so every device drops it).
	socket.on('deleteSet', function(payload) {
		try {
			if (!payload || typeof payload !== 'object') return;
			var machinePosition = parseInt(payload.machinePosition, 10);
			var setIndex = parseInt(payload.setIndex, 10);
			if (Number.isNaN(machinePosition) || Number.isNaN(setIndex)) return;
			var key = machinePosition + '_' + setIndex;
			delete slotDescriptors[key];
			delete slotEnabledState[key];
			delete savedSets[key];
			if (io) io.sockets.emit('set-deleted', {
				machinePosition: machinePosition,
				setIndex: setIndex
			});
		} catch (e) {
			console.log('[set] deleteSet failed: ' + e.message);
		}
	});

	// Live editing session (selections, tempo, single-shot pick, run options,
	// live court slider values). Stored and relayed to every OTHER device so all
	// screens mirror the same in-progress config.
	socket.on('uiState', function(state) {
		if (!state || typeof state !== 'object') return;
		sharedUiState = state;
		socket.broadcast.emit('uiState', state);
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

			if (!ensureOperationReady(socket, 'setMachine')) {
				socket.emit('machine-ready', true);
				return;
			}
			
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
			
			// The native operation is asynchronous. Keep the UI interlocked until
			// its worker reports that tilt/speed configuration has really finished.
			socket.emit('machine-ready', false);
			var accepted = operation.setMachine(machinePosition, setIndex, function(err) {
				if (err) {
					console.log('[operation] setMachine worker failed: ' + err.message);
					socket.emit('machine-error', err.message || 'Failed to apply set machine.');
				}
				socket.emit('machine-ready', true);
				socket.emit('set-machine-state', err ? 'FAILED' : 'APPLIED');
			});
			if (accepted === false) {
				socket.emit('machine-ready', true);
				socket.emit('machine-error', 'Machine is still applying the previous set.');
			}
		} catch (e) {
			console.log('[operation] setMachine failed: ' + e.message);
			socket.emit('machine-ready', true);
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
		socket.emit('tilt-state', 'MOVING');
		var accepted = operation.tiltSignal(value, function(err) {
			if (err) {
				socket.emit('machine-error', err.message || 'Failed to change angle.');
			}
			socket.emit('tilt-state', err ? 'FAILED' : 'IDLE');
		});
		if (accepted === false) {
			socket.emit('tilt-state', 'IDLE');
			socket.emit('machine-error', 'The linear actuator is already changing angle.');
		}
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
		var started = operation.hopperStart();
		if (started === false) {
			socket.emit('machine-error', 'Cannot start hopper: machine is not running.');
		}
	});

	socket.on('hopper-off', function() {
		if (!ensureOperationReady(socket, 'hopper-off')) return;
		console.log("Got hopper-off command.");
		operation.hopperStop();
	});

	socket.on('hopper-pulse', function() {
		if (!ensureOperationReady(socket, 'hopper-pulse')) {
			socket.emit('machine-ready', true);
			return;
		}
		console.log("Got hopper-pulse command.");
		socket.emit('machine-ready', false);
		var pulsed = operation.hopperPulse();
		if (pulsed === false) {
			socket.emit('machine-error', 'Cannot pulse hopper: machine is not running.');
		}
		socket.emit('machine-ready', true);
		var pulseCount = 0;
		try { pulseCount = operation.getHopperPulseCount(); } catch (e) { pulseCount = 0; }
		socket.emit('hopper-pulse-complete', pulsed !== false, pulseCount);
	});

	socket.on('homing-sequence', function() {
		if (!ensureOperationReady(socket, 'homing-sequence')) {
			socket.emit('homing-error', 'Machine control is not ready.');
			return;
		}
		console.log("Got homing-sequence command.");
		try {
			operation.homingSequence();
			socket.emit('homing-complete');
		} catch (e) {
			console.error('[operation] Homing sequence failed: ' + e.message);
			socket.emit('homing-error', 'Homing sequence failed.');
		}
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
		var pose = parsePoseLine(msg.toString('utf8').trim());
		if (!pose || !io) return;
		// The pose recogniser is a dumb sensor: it reports whether a person is
		// present and whether an arm is raised (wrist above shoulder). The browser
		// watches for a raised arm held ~3s to start a run (see
		// handleLaunchGestureFrame in public/index.html).
		io.sockets.emit('gesture-update', { present: pose.present, armRaised: pose.armRaised });
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

// "pose <present> <arm_raised>" -> { present, armRaised } (each 0/1), or null.
// The recogniser (m2demo) reports one person's pose: whether it's visible and
// whether either wrist is above the matching shoulder (an arm raised to signal
// "start"). Kept trivial so the recogniser stays a dumb sensor and the start
// policy lives in the browser.
function parsePoseLine(line) {
	var parts = line.split(/\s+/);
	if (parts[0] !== 'pose') return null;
	return {
		present: parts[1] === '1',
		armRaised: parts[2] === '1'
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

"use strict";
// Client-side gesture-control UI for the BeagleY-AI.
// Talks to server/lib/gesture_server.js over socket.io.

var socket = io.connect();
var communicationsTimeout = null;
var hideErrorTimeout = null;

var FINGER_KEYS = ["thumb", "index", "middle", "ring", "pinky"];

$(document).ready(function () {
	$('#error-box').hide();
	setupServerMessageHandlers(socket);

	$("#startBtn").click(function () {
		sendCommandToServer("start");
		appendStatus("Requesting recogniser start...");
	});

	$("#stopBtn").click(function () {
		sendCommandToServer("stop");
		appendStatus("Requesting recogniser stop...");
	});
});

function setupServerMessageHandlers(socket) {
	socket.on('gesture-update', function (data) {
		clearServerTimeout();
		renderGesture(data);
	});

	socket.on('state-reply', function (state) {
		setRunningState(state === 'RUNNING');
		appendStatus("State: " + state);
	});

	socket.on('start-reply', function (message) {
		appendStatus("Start: " + message);
		if (message === 'STARTED' || message === 'ALREADY_RUNNING') {
			setRunningState(true);
		}
		clearServerTimeout();
	});

	socket.on('stop-reply', function (message) {
		appendStatus("Stop: " + message);
		clearServerTimeout();
	});

	socket.on('recognizer-stopped', function (code) {
		appendStatus("Recogniser stopped (" + code + ").");
		setRunningState(false);
		renderGesture(null);
	});

	socket.on('gesture-error', errorHandler);
}

// ---- Rendering -------------------------------------------------------------
function renderGesture(data) {
	if (!data) {
		$("#gestureName").text("NONE");
		$("#fingerCount").text("0");
		FINGER_KEYS.forEach(function (k) {
			setFinger(k, false);
		});
		return;
	}

	$("#gestureName").text(data.name || "UNKNOWN");
	$("#fingerCount").text(data.count != null ? data.count : 0);

	var fingers = data.fingers || {};
	FINGER_KEYS.forEach(function (k) {
		setFinger(k, !!fingers[k]);
	});
}

function setFinger(key, up) {
	var $chip = $("#finger-" + key);
	$chip.toggleClass("up", up);
	$chip.toggleClass("down", !up);
	$chip.find(".finger-state").text(up ? "UP" : "down");
}

function setRunningState(running) {
	$("#stateid").text(running ? "Running" : "Idle");
	$("#statePill").text(running ? "Running" : "Idle");
	$("#statePill").toggleClass("on", running);
	$("#startBtn").prop("disabled", running);
	$("#stopBtn").prop("disabled", !running);
}

// ---- Comms helpers ---------------------------------------------------------
function sendCommandToServer(command, options) {
	if (communicationsTimeout == null) {
		communicationsTimeout = setTimeout(errorHandler, 1500,
			"ERROR: Unable to communicate with server. Is the nodeJS server running?");
	}
	socket.emit(command, options);
}

function clearServerTimeout() {
	clearTimeout(communicationsTimeout);
	communicationsTimeout = null;
}

function appendStatus(line) {
	var $status = $("#status");
	var current = $status.text() || "";
	var next = (current ? current + "\n" : "") + line;
	$status.text(next);
	$status.scrollTop($status[0].scrollHeight);
}

function errorHandler(message) {
	console.log("ERROR Handler: " + message);
	$('#error-text').text(message);
	$('#error-box').show();
	window.clearTimeout(hideErrorTimeout);
	hideErrorTimeout = window.setTimeout(function () { $('#error-box').hide(); }, 5000);
	clearServerTimeout();
}

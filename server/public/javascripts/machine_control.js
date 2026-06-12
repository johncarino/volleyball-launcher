"use strict";
// Machine Control module - handles machine controls

$(document).ready(function () {
	setupSliderControls();
	setupMachineControls();
});

function setupSliderControls() {
	var $speed = $("#speedSlider");
	var $angle = $("#angleSlider");

	function updateSpeedDisplay() {
		var v = $speed.val();
		$("#speedValue").text(v);
	}

	function logSpeedChange() {
		logMachineValue("Speed", $speed.val());
	}

	function updateAngleDisplay() {
		var v = $angle.val();
		$("#angleValue").text(v);
	}

	function logAngleChange() {
		logMachineValue("Angle", $angle.val());
	}

	$speed.on("input", updateSpeedDisplay);
	$speed.on("change", logSpeedChange);

	$angle.on("input", updateAngleDisplay);
	$angle.on("change", logAngleChange);

	// initialize displayed values
	updateSpeedDisplay();
	updateAngleDisplay();
	logMachineValue("Speed", $speed.val());
	logMachineValue("Angle", $angle.val());
}

function logMachineValue(label, value) {
	console.log(label + ":", value);
}

function setupMachineControls() {
	$("#machineStopBtn").click(function () {
		$("#speedSlider").val(0);
		$("#speedValue").text(0);
		logMachineValue("Speed", 0);
	});
}

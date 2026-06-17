// Ensure Socket.IO is loaded and available
if (typeof io === 'undefined') {
  console.error('Socket.IO client library not loaded.');
} else {
  const socket = io(); // Connects to the default namespace

  // Get references to the sliders and their value displays
  const speedSlider = $('#speedSlider');
  const speedValueSpan = $('#speedValue');
  const angleSlider = $('#angleSlider');
  const angleValueSpan = $('#angleValue');
  const machineStopBtn = $('#machineStopBtn');

  // Function to update slider display and emit to server
  function updateSliderAndEmit(slider, valueSpan, eventName) {
    const value = slider.val();
    valueSpan.text(value);
    console.log(`Emitting ${eventName}: ${value}`);
    socket.emit(eventName, value);
  }

  // Event listener for Speed Slider changes
  speedSlider.on('change', function() {
    updateSliderAndEmit($(this), speedValueSpan, 'setSpeed');
  });

  // Event listener for Angle Slider changes
  angleSlider.on('change', function() {
    updateSliderAndEmit($(this), angleValueSpan, 'setAngle');
  });

  // Event listener for Stop Motors button
  machineStopBtn.on('click', function() {
    console.log('Emitting stopMotors');
    socket.emit('stopMotors');
    // Optionally reset sliders to 0 after stopping
    speedSlider.val(0).trigger('input'); // Trigger input to update display
  });

  // Initialize slider displays on page load
  $(document).ready(function() {
    speedValueSpan.text(speedSlider.val());
    angleValueSpan.text(angleSlider.val());
  });
}
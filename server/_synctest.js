// Temp sync test: two clients, verify broadcasts reach the OTHER client.
require('./server.js'); // starts http+socket.io on 8088
var ioc = require('socket.io-client');

function mk(name) {
  var s = ioc('http://localhost:8088', { transports: ['websocket'], forceNew: true });
  s.on('connect', function(){ console.log(name + ' connected'); s.emit('page-loaded'); });
  return s;
}

var results = {};
var A = mk('A');
var B = mk('B');

B.on('state-snapshot', function(){ results.B_snapshot = true; console.log('B got state-snapshot'); });
B.on('set-deleted', function(d){ results.B_setDeleted = true; console.log('B got set-deleted', d); });
B.on('calibration-updated', function(c){ results.B_calib = true; console.log('B got calibration-updated', c); });

setTimeout(function(){
  console.log('--- A emits deleteSet + calibration change ---');
  A.emit('deleteSet', { machinePosition: 0, setIndex: 0 });
  A.emit('setNetHeight', 2.5);
  A.emit('recalculateSets');
}, 800);

setTimeout(function(){
  console.log('=== RESULTS ===', JSON.stringify(results));
  process.exit(0);
}, 2000);

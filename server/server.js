"use strict";

// Server for HTTP and loading other server modules
// Install modules:
//   $ npm install
//
// Launch server with:
//   $ node server.js
// 

var PORT_NUMBER = 8088;

var http = require('http');
var fs   = require('fs');
var path = require('path');
var mime = require('mime');


/* 
 * Create the static web server
 */
var server = http.createServer(function (request, response) {
  // strip querystring
  var urlPath = request.url.split('?')[0];

  var filePath;
  if (urlPath === '/') filePath = 'public/index.html';
  else filePath = 'public' + urlPath;

  // IMPORTANT: resolve relative to this server.js location
  var absPath = path.join(__dirname, filePath);
  serveStatic(response, absPath);
});

server.listen(PORT_NUMBER, '0.0.0.0', function() {
  console.log("Server listening on port " + PORT_NUMBER);
  console.log("Access from other machines at: http://YOUR_IP:" + PORT_NUMBER);
});


function serveStatic(response, absPath) {
	fs.exists(absPath, function(exists) {
		if (exists) {
			fs.readFile(absPath, function(err, data) {
				if (err) {
					send404(response);
				} else {
					sendFile(response, absPath, data);
				}
			});
		} else {
			send404(response);
		}
	});
}

function send404(response) {
	response.writeHead(404, {'Content-Type': 'text/plain'});
	response.write('Error 404: resource not found.');
	response.end();
}

function sendFile(response, filePath, fileContents) {
	response.writeHead(
			200,
			{"content-type": mime.lookup(path.basename(filePath))}
		);
	response.end(fileContents);
}


/*
 * Load the gesture-control websocket + UDP backend
 */
var procServer = require('./lib/gesture_server');
procServer.listen(server);

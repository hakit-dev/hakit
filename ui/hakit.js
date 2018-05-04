/*
 * HAKit - The Home Automation Kit
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * JS WebSocket client library
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

const HAKIT_ST_IDLE = 0;
const HAKIT_ST_PROPS = 1;
const HAKIT_ST_READY = 2;
const HAKIT_ST_GET = 3;
const HAKIT_ST_TRACE = 4;

var hakit_sock;
var hakit_sock_state = HAKIT_ST_IDLE;
var hakit_sock_timeout;
var hakit_sock_failures = 0;
var hakit_props = {};
var hakit_t0 = 0;


function get_appropriate_ws_url()
{
    var pcol;
    var u = document.URL;

    /*
     * We open the websocket encrypted if this page came on an
     * https:// url itself, otherwise unencrypted
     */

    if (u.substring(0, 5) == "https") {
	pcol = "wss://";
	u = u.substr(8);
    } else {
	pcol = "ws://";
	if (u.substring(0, 4) == "http")
	    u = u.substr(7);
    }

    u = u.split('/');

    /* + "/xxx" bit is for IE10 workaround */

    return pcol + u[0] + "/xxx";
}


function hakit_send(msg)
{
    console.log("hakit_send('"+msg+"')");
    if (hakit_sock) {
	hakit_sock.send(msg+"\n");
    }
}

function hakit_set(name, value)
{
    hakit_send('set '+name+'="'+value+'"');
}


function hakit_get(name)
{
    if (hakit_sock_state == HAKIT_ST_READY) {
	if (name) {
	    hakit_send("get "+name);
	}
	else {
	    hakit_send("get");
	}
	hakit_sock_state = HAKIT_ST_GET;
    }
    else {
	console.log("WARNING: Attempting to send command 'get' in sock state "+hakit_sock_state);
    }
}


function hakit_recv_props(line)
{
    var i = line.indexOf(':');
    var key = line.substr(0,i);
    var value = line.substr(i+1).trim();
    hakit_props[key] = value;

    if (key == 'T0') {
        hakit_t0 = parseInt(value);
    }
}


function hakit_recv_get(line)
{
    var fields = line.split(" ");
    var dir = fields[0];
    var widget = fields[1];
    var chart_args = fields[2].split(",");
    var signal_name = fields[3];

    var value = '';
    for (var i = 4; i < fields.length; i++) {
	value += ' '+fields[i];
    }
    value = value.trim();

    hakit_updated(signal_name, value, dir, widget);

    var chart_name = chart_args[0];
    if (chart_name != '-') {
        hakit_chart_add(chart_name, signal_name, chart_args[1]);
    }
}


function hakit_recv_trace(line)
{
    //console.log("hakit_recv_trace("+line+")");
    var fields = line.split(" ");
    var name = fields[0];

    var data = [];
    for (var i = 1; i < fields.length; i++) {
        var tab = fields[i].split(",");
        var pt = {
            t: parseInt(tab[0]) + hakit_t0,
            y: tab[1],
            ext: (tab[0].substr(0,1) == '+'),
        }
        data.push(pt);
    }
    hakit_chart_set(name, data);
}


function hakit_recv_event(line)
{
    var i = line.indexOf("=");
    if (i > 1) {
        var tab = line.substr(1,i-1).split(',');
        var signal_spec = tab.pop();
        var t = tab.pop();
	var value = line.substr(i+1);
	hakit_updated(signal_spec, value);

        if ((typeof hakit_chart_enabled === "function") && hakit_chart_enabled()) {
            if (t) {
                var pt = {
                    t: parseInt(t) + hakit_t0,
                    y: value,
                }
	        hakit_chart_updated(signal_spec, pt);
            }
        }
    }
}


function hakit_recv_line(line)
{
    //console.log("hakit_recv_line('"+line+"')");

    if (line.substr(0,1) == "!") {
        hakit_recv_event(line);
    }
    else {
	if (line == ".") {
	    if (hakit_sock_state == HAKIT_ST_PROPS) {
		hakit_connected(true);
		hakit_send("get");
		hakit_sock_state = HAKIT_ST_GET;
	    }
	    else if (hakit_sock_state == HAKIT_ST_GET) {
                if ((typeof hakit_chart_enabled === "function") && hakit_chart_enabled()) {
                    var depth = undefined;
                    if (hakit_props['TRACE_DEPTH']) {
                        depth = parseInt(hakit_props['TRACE_DEPTH']);
                    }
                    hakit_chart_init(depth);
		    hakit_send("trace");
		    hakit_sock_state = HAKIT_ST_TRACE;
                }
                else {
		    hakit_sock_state = HAKIT_ST_READY;
                }
	    }
	    else {
		hakit_sock_state = HAKIT_ST_READY;
            }
	}
	else {
	    if (hakit_sock_state == HAKIT_ST_PROPS) {
                hakit_recv_props(line);
	    }
	    else if (hakit_sock_state == HAKIT_ST_GET) {
                hakit_recv_get(line);
	    }
	    else if (hakit_sock_state == HAKIT_ST_TRACE) {
                hakit_recv_trace(line);
            }
	}
    }
}


function hakit_connect()
{
    console.log("hakit_connect() "+hakit_sock_state);

    /* Clear pending connect timeout (if any) */
    if (hakit_sock_timeout) {
	window.clearTimeout(hakit_sock_timeout);
	hakit_sock_timeout = undefined;
    }

    /* Setup WebSocket connection */
    if (typeof MozWebSocket != "undefined") {
	hakit_sock = new MozWebSocket(get_appropriate_ws_url(), "hakit-events-protocol");
    } else {
	hakit_sock = new WebSocket(get_appropriate_ws_url(), "hakit-events-protocol");
    }

    try {
	hakit_sock.onopen = function() {
	    console.log("hakit_connect: connection established");
	    hakit_sock_state = HAKIT_ST_PROPS;
	    hakit_send("props");
	} 

	hakit_sock.onmessage = function got_packet(msg) {
	    var lines = msg.data.split("\n");
	    //console.log("=== ["+msg.data.length+"] "+lines.length+" '"+msg.data+"'");
	    for (var i = 0; i < lines.length; i++) {
		var line = lines[i];
		if (line != "") {
		    hakit_recv_line(line);
		}
	    }
	    //console.log("===");
	} 

	hakit_sock.onclose = function() {
	    hakit_sock = undefined;

	    if (hakit_sock_state != HAKIT_ST_IDLE) {
		console.log("hakit_connect: connection closed");
		hakit_sock_state = HAKIT_ST_IDLE;
		hakit_connected(false);
		hakit_sock_failures = 0;
	    }
	    else {
		hakit_sock_failures++;
		console.log("hakit_connect: connection failure ("+hakit_sock_failures+")");
	    }

	    if (hakit_sock_failures < 60) {
		hakit_sock_timeout = setTimeout(hakit_connect, 10000);
	    }
	}
    } catch(exception) {
	alert('<p>ERROR: ' + exception + '</p>');  
    }
}

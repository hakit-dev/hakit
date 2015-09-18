
const ST_IDLE = 0;
const ST_READY = 1;
const ST_GET_CMD = 2;
const ST_GET_RSP = 3;

var hakit_sock;
var hakit_sock_state = ST_IDLE;
var hakit_signals;
var hakit_dashboard;


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


function hakit_send(msg) {
    console.log("hakit_send('"+msg+"')");
    if (hakit_sock) {
	hakit_sock.send(msg+"\n");
    }
}

function hakit_update(id, value) {
    hakit_send('set '+id+'="'+value+'"');
}


function clear_signals()
{
    if (hakit_signals) {
	while (hakit_signals.rows.length > 1) {
	    hakit_signals.deleteRow(1);
	}
    }
}


function signal_update(elmt, st)
{
    var id = elmt.id.split(':')[0];
    console.log("signal_update "+id+" "+st);
    hakit_update(id, st);
}


function switch_clicked(elmt)
{
    var st = elmt.checked ? 1:0;
    signal_update(elmt, st);
}


function switch_release(elmt)
{
    elmt.checked = false;
    signal_update(elmt, 0);
}


function widget_led(widget, value)
{
    if ((value != '') && (value != '0')) {
	str = '<div class="led '+widget+'"></div>';
    }
    else {
	str = '<div class="led led"></div>';
    }

    return str;
}


function widget_switch_slide(id, value)
{
    var str = '<div class="switch slide"><input type="checkbox" id="'+id+':switch" onclick="switch_clicked(this);"';
    if ((value != '0') && (value != '')) {
	str += ' checked';
    }
    str += '><label><i></i></label></div>';

    return str;
}


function widget_switch_push(id, value)
{
    var str = '<div class="switch push"><input type="checkbox" id="'+id+':switch" onmousedown="signal_update(this,1);" onmouseup="switch_release(this);" onmouseout="switch_release(this);";';
    if ((value != '0') && (value != '')) {
	str += ' checked';
    }
    str += '><label><i></i></label></div>';

    return str;
}


function widget_slider(id, value, options)
{
    var str = '<input type="range" class="slider" id="'+id+':slider" onchange="signal_update(this,this.value);"';

    if (value) {
	str += ' value="'+value+'"';
    }

    var list = options.split(',');
    for (var i = 0; i < list.length; i++) {
	str += ' '+list[i];
    }

    str += ' />';

    return str;
}


function widget_meter(id, value, options)
{

    var str = '<meter id="'+id+':meter"';

    if (value) {
	str += ' value="'+value+'"';
    }

    var list = options.split(',');
    for (var i = 0; i < list.length; i++) {
	str += ' '+list[i];
    }

    str += '>'+value+'</meter>';

    return str;
}


function add_signal(line)
{
    console.log("add_signal('"+line+"')");

    var fields = line.split(" ");
    var dir = fields[0];
    var widget = fields[1];
    var name = fields[2];
    var value = fields[3];

    if (hakit_dashboard) {
	dashboard_update(name, value);
    }

    if (hakit_signals) {
	var len = hakit_signals.rows.length;
	var row = hakit_signals.insertRow(len);
	if (len % 2) {
	    row.className = "pure-table-odd";
	}

	row.insertCell(0).innerHTML = dir;
	row.insertCell(1).innerHTML = name;
	row.insertCell(2).innerHTML = value;

	var str = "";
	if (widget.substr(0,4) == 'led-') {
	    str = widget_led(widget, value);
	}
	else if (widget == 'switch-slide') {
	    str = widget_switch_slide(name, value);
	}
	else if (widget == 'switch-push') {
	    str = widget_switch_push(name, value);
	}
	else if (widget.substr(0,5) == 'meter') {
	    var args = widget.split(':');
	    str = widget_meter(name, value, args[1]);
	}
	else if (widget.substr(0,6) == 'slider') {
	    var args = widget.split(':');
	    str = widget_slider(name, value, args[1]);
	}

	row.insertCell(3).innerHTML = str;

	var cell = row.insertCell(4);
	cell.hidden = true;
	cell.innerHTML = widget;
    }
}


function update_signal(line)
{
    console.log("update_signal('"+line+"')");

    var i = line.indexOf(" ");
    if (i <= 0) {
	return;
    }

    var name = line.substr(0,i);
    var value = line.substr(i+1);

    if (hakit_dashboard) {
	dashboard_update(name, value);
    }

    if (hakit_signals) {
	var rows = hakit_signals.rows;
	// Update row with new value
	for (var i = 0; i < rows.length; i++) {
	    var row = rows[i];

	    if (row.cells[1].innerHTML == name) {
		row.cells[2].innerHTML = value;
		var widget = row.cells[4].innerHTML;

		if (widget.substr(0, 4) == 'led-') {
		    row.cells[3].innerHTML = widget_led(widget, value);
		}
		else {
		    var elmt = document.getElementById(name+':switch');
		    if (elmt) {
			elmt.checked = ((value != '') && (value != '0'));
		    }

		    elmt = document.getElementById(name+':meter');
		    if (elmt) {
			elmt.value = value;
		    }

		    elmt = document.getElementById(name+':slider');
		    if (elmt) {
			elmt.value = value;
		    }
		}
		return;
	    }
	}

	// If row not found, refresh the whole list
	get_all();
    }
}


function recv_line(line) {
    //console.log("recv_line('"+line+"')");

    if (line == ".") {
	hakit_sock_state = ST_READY;
    }
    else if (line.substr(0,1) == "!") {
	update_signal(line.substr(1));
    }
    else {
	switch (hakit_sock_state) {
	    case ST_READY:
	        break;
	    case ST_GET_CMD:
	        hakit_sock_state = ST_GET_RSP;
	        clear_signals();
	    case ST_GET_RSP:
	        add_signal(line);
	        break;
	}
    }
}


function get_all() {
    if (hakit_sock_state == ST_READY) {
	hakit_send("get");
	hakit_sock_state = ST_GET_CMD;
    }
    else {
	console.log("WARNING: Attempting to send command in sock state "+hakit_sock_state);
    }
}


/* Detect and show browser */
var browser = document.getElementById("brow");
if (browser) {
    BrowserDetect.init();
    browser.textContent = " " + BrowserDetect.browser + " " + BrowserDetect.version +" " + BrowserDetect.OS +" ";
}

/* Get document elements */
hakit_signals = document.getElementById("signals");
hakit_dashboard = document.getElementById("dashboard");

/* Setup WebSocket connection */
if (typeof MozWebSocket != "undefined") {
    hakit_sock = new MozWebSocket(get_appropriate_ws_url(), "hakit-events-protocol");
} else {
    hakit_sock = new WebSocket(get_appropriate_ws_url(), "hakit-events-protocol");
}

try {
    hakit_sock.onopen = function() {
	hakit_sock_state = ST_READY;

	if (hakit_signals) {
	    document.getElementById("ws_status_td").style.backgroundColor = "#40ff40";
	    document.getElementById("ws_status").textContent = "Connected";
	}
	if (hakit_dashboard) {
	    dashboard_connect(true);
	}
	get_all();
    } 

    hakit_sock.onmessage = function got_packet(msg) {
	var lines = msg.data.split("\n");
	//console.log("=== ["+msg.data.length+"] "+lines.length+" '"+msg.data+"'");
	for (var i = 0; i < lines.length; i++) {
	    var line = lines[i];
	    if (line != "") {
		recv_line(line);
	    }
	}
	//console.log("===");
    } 

    hakit_sock.onclose = function(){
	hakit_sock_state = ST_IDLE;

	if (hakit_signals) {
	    document.getElementById("ws_status_td").style.backgroundColor = "#ff4040";
	    document.getElementById("ws_status").textContent = "Disconnected";
	}
	if (hakit_dashboard) {
	    dashboard_connect(false);
	}
    }
} catch(exception) {
    alert('<p>ERROR: ' + exception + '</p>');  
}

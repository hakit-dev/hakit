var sock;

const ST_READY = 0;
const ST_GET_CMD = 1;
const ST_GET_RSP = 2;
var sock_state = ST_READY;

const FLAG_EVENT = 0x01;
const FLAG_PRIVATE = 0x02;


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


function clear_signals()
{
    var signals = document.getElementById("signals");
    while (signals.rows.length > 1) {
	signals.deleteRow(1);
    }
}


function switch_update(elmt, st)
{
    console.log("switch_update "+elmt.id+" "+st);
    sock.send("set "+elmt.id+"="+st+"\n");
}


function switch_clicked(elmt)
{
    var st = elmt.checked ? 1:0;
    switch_update(elmt, st);
}


function switch_release(elmt)
{
    elmt.checked = false;
    switch_update(elmt, 0);
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
    var str = '<div class="switch slide"><input type="checkbox" id="'+id+'" onclick="switch_clicked(this);"';
    if ((value != '0') && (value != '')) {
	str += ' checked';
    }
    str += '><label><i></i></label></div>';

    return str;
}


function widget_switch_push(id, value)
{
    /*var str = '<div class="switch push"><input type="button" id="'+id+'" onmousedown="switch_update(this,1);" onmouseup="switch_update(this,0);"';
    if ((value != '0') && (value != '')) {
	str += ' active';
    }
    str += '><label></label></div>';*/
    var str = '<div class="switch push"><input type="checkbox" id="'+id+'" onmousedown="switch_update(this,1);" onmouseup="switch_release(this);"';
    if ((value != '0') && (value != '')) {
	str += ' checked';
    }
    str += '><label><i></i></label></div>';

    return str;
}


function add_signal(line)
{
    console.log("add_signal('"+line+"')");
    var signals = document.getElementById("signals");
    var len = signals.rows.length;
    var row = signals.insertRow(len);
    if (len % 2) {
	row.className = "pure-table-odd";
    }

    var fields = line.split(" ");
    var dir = fields[0];
    var widget = fields[1];
    var name = fields[2];
    var value = fields[3];

    row.insertCell(0).innerHTML = dir;
    row.insertCell(1).innerHTML = name;
    row.insertCell(2).innerHTML = value;

    var str = "";
    if (widget.substr(0, 4) == 'led-') {
	str = widget_led(widget, value);
    }
    else if (widget == 'switch-slide') {
	str = widget_switch_slide(name, value);
    }
    else if (widget == 'switch-push') {
	str = widget_switch_push(name, value);
    }

    row.insertCell(3).innerHTML = str;
}


function update_signal(line)
{
    console.log("update_signal('"+line+"')");
    var signals = document.getElementById("signals");
    var rows = signals.rows;
    var fields = line.split(" ");
    var dir = fields[0];
    var widget = fields[1];
    var name = fields[2];
    var value = fields[3];

    // Update row with new value
    for (var i = 0; i < rows.length; i++) {
	var row = rows[i];

	if (row.cells[1].innerHTML == name) {
	    row.cells[2].innerHTML = value;

	    if (widget.substr(0, 4) == 'led-') {
		row.cells[3].innerHTML = widget_led(widget, value);
	    }
	    else {
		var control = document.getElementById(name);
		if (control) {
		    control.checked = ((value != '') && (value != '0'));
		}
	    }
	    return;
	}
    }

    // If row not found, refresh the whole list
    get_all();
}


function recv_line(line) {
    //console.log("recv_line('"+line+"')");

    if (line == ".") {
	sock_state = ST_READY;
    }
    else if (line.substr(0,1) == "!") {
	update_signal(line.substr(1));
    }
    else {
	switch (sock_state) {
	    case ST_READY:
	        break;
	    case ST_GET_CMD:
	        sock_state = ST_GET_RSP;
	        clear_signals();
	    case ST_GET_RSP:
	        add_signal(line);
	        break;
	}
    }
}


function get_all() {
    if (sock_state == ST_READY) {
	sock.send("get\n");
	sock_state = ST_GET_CMD;
    }
    else {
	console.log("WARNING: Attempting to send command in sock state "+sock_state);
    }
}


/* Detect and show browser */
BrowserDetect.init();
document.getElementById("brow").textContent = " " + BrowserDetect.browser + " "
	+ BrowserDetect.version +" " + BrowserDetect.OS +" ";

/* Setup WebSocket connection */
if (typeof MozWebSocket != "undefined") {
    sock = new MozWebSocket(get_appropriate_ws_url(), "hakit-events-protocol");
} else {
    sock = new WebSocket(get_appropriate_ws_url(), "hakit-events-protocol");
}

try {
    sock.onopen = function() {
	document.getElementById("ws_status_td").style.backgroundColor = "#40ff40";
	document.getElementById("ws_status").textContent = "Connected";
	get_all();
    } 

    sock.onmessage = function got_packet(msg) {
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

    sock.onclose = function(){
	document.getElementById("ws_status_td").style.backgroundColor = "#ff4040";
	document.getElementById("ws_status").textContent = "Disconnected";
    }
} catch(exception) {
    alert('<p>ERROR: ' + exception + '</p>');  
}

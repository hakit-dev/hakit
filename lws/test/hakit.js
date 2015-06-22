BrowserDetect.init();

document.getElementById("brow").textContent = " " + BrowserDetect.browser + " "
	+ BrowserDetect.version +" " + BrowserDetect.OS +" ";

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
    for (var i = 0; i < fields.length; i++) {
	row.insertCell(i).innerHTML = fields[i];
    }
}

function update_signal(line)
{
    console.log("update_signal('"+line+"')");
    var signals = document.getElementById("signals");
    var rows = signals.rows;
    var fields = line.split(" ");

    // Update row with new value
    for (var i = 0; i < rows.length; i++) {
	var row = rows[i];
	if (row.cells[2].innerHTML == fields[2]) {
	    row.cells[0].innerHTML = fields[0];
	    row.cells[1].innerHTML = fields[1];
	    row.cells[3].innerHTML = fields[3];
	    return;
	}
    }

    // If row not found, refresh the whole list
    get_all();
}


var sock;

const ST_READY = 0;
const ST_GET_CMD = 1;
const ST_GET_RSP = 2;
var sock_state = ST_READY;


function recv_line(line) {
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
	for (var i = 0; i < lines.length; i++) {
	    var line = lines[i];
	    if (line != "") {
		recv_line(line);
	    }
	}
    } 

    sock.onclose = function(){
	document.getElementById("ws_status_td").style.backgroundColor = "#ff4040";
	document.getElementById("ws_status").textContent = "Disconnected";
    }
} catch(exception) {
    alert('<p>ERROR: ' + exception + '</p>');  
}


$('.button-wrap').on("click", function(){
  $(this).toggleClass('button-active');
});

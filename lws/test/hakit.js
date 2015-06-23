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


function switch_clicked(elmt)
{
    console.log("switch_clicked "+elmt.id+" "+elmt.checked);
    var st = elmt.checked ? 1:0;
    sock.send("set "+elmt.id+"="+st+"\n");
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
    var i;
    for (i = 0; (i < fields.length) && (i < 5); i++) {
	row.insertCell(i).innerHTML = fields[i];
    }

    var str = "";
    if ((fields[0] == 'sink') && (fields[1] & FLAG_PRIVATE)) {
	var id = fields[2];
	str = '<div class="switch demo3"><input type="checkbox" id="'+id+'" onclick="switch_clicked(this);"';
	if ((fields[3] != '0') && (fields[3] != '')) {
	    str += ' checked';
	}
	str += '><label><i></i></label></div>';
    }

    row.insertCell(4).innerHTML = str;
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
	var id = fields[2];
	if (row.cells[2].innerHTML == id) {
	    var value = fields[3];
	    row.cells[3].innerHTML = value;
	    if ((value != '') && (value != '0')) {
		document.getElementById(id).checked = true;
	    }
	    else {
		document.getElementById(id).checked = false;
	    }
	    return;
	}
    }

    // If row not found, refresh the whole list
    get_all();
}


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

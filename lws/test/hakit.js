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


var sock;

if (typeof MozWebSocket != "undefined") {
    sock = new MozWebSocket(get_appropriate_ws_url(), "hakit-events-protocol");
} else {
    sock = new WebSocket(get_appropriate_ws_url(), "hakit-events-protocol");
}

try {
    sock.onopen = function() {
	document.getElementById("ws_status_td").style.backgroundColor = "#40ff40";
	document.getElementById("ws_status").textContent = "Connected";
    } 

    sock.onmessage = function got_packet(msg) {
//	document.getElementById("number").textContent = msg.data + "\n";
	document.forms.f.output.value = msg.data + "\n";
    } 

    sock.onclose = function(){
	document.getElementById("ws_status_td").style.backgroundColor = "#ff4040";
	document.getElementById("ws_status").textContent = "Disconnected";
    }
} catch(exception) {
    alert('<p>ERROR: ' + exception + '</p>');  
}

function get_all() {
    sock.send("get\n");
}

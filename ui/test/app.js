BrowserDetect.init();

document.getElementById("brow").textContent = " " + BrowserDetect.browser + " " + BrowserDetect.version +" " + BrowserDetect.OS +" ";

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


document.getElementById("number").textContent = get_appropriate_ws_url();

/* dumb increment protocol */
	
var socket_di;

if (typeof MozWebSocket != "undefined") {
    socket_di = new MozWebSocket(get_appropriate_ws_url(),
				 "dumb-increment-protocol");
} else {
    socket_di = new WebSocket(get_appropriate_ws_url(),
			      "dumb-increment-protocol");
}

try {
    socket_di.onopen = function() {
	document.getElementById("wsdi_statustd").style.backgroundColor = "#40ff40";
	document.getElementById("wsdi_status").textContent = " websocket connection opened ";
    } 

    socket_di.onmessage =function got_packet(msg) {
	document.getElementById("number").textContent = msg.data + "\n";
    } 

    socket_di.onclose = function(){
	document.getElementById("wsdi_statustd").style.backgroundColor = "#ff4040";
	document.getElementById("wsdi_status").textContent = " websocket connection CLOSED ";
    }
} catch(exception) {
    alert('<p>Error' + exception);  
}

function reset() {
    socket_di.send("reset\n");
}

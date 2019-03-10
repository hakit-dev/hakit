/*
 * HAKit - The Home Automation Kit
 * Copyright (C) 2014-2016 Sylvain Giroudon
 *
 * Dynamic table of source/sink signals
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

var hakit_signals = {};


function hakit_widget_updated(elmt, st)
{
    var id = elmt.id.split(':')[0];
    console.log("hakit_widget_updated "+id+" "+st);
    hakit_set(id, st);
}


function hakit_widget_clicked(elmt)
{
    var st = elmt.checked ? 1:0;
    hakit_widget_updated(elmt, st);
}


function hakit_widget_release(elmt)
{
    elmt.checked = false;
    hakit_widget_updated(elmt, 0);
}


function hakit_widget_led(widget, value)
{
    if ((value != '') && (value != '0')) {
	str = '<div class="led '+widget+'"></div>';
    }
    else {
	str = '<div class="led led"></div>';
    }

    return str;
}


function hakit_widget_switch_slide(id, value)
{
    var str = '<div class="switch slide"><input type="checkbox" id="'+id+':switch" onclick="hakit_widget_clicked(this);"';
    if ((value != '0') && (value != '')) {
	str += ' checked';
    }
    str += '><label><i></i></label></div>';

    return str;
}


function hakit_widget_switch_push(id, value)
{
    var str = '<div class="switch push"><input type="checkbox" id="'+id+':switch" onmousedown="hakit_widget_updated(this,1);" onmouseup="hakit_widget_release(this);" onmouseout="hakit_widget_release(this);"';
    if ((value != '0') && (value != '')) {
	str += ' checked';
    }
    str += '><label><i></i></label></div>';

    return str;
}


function hakit_widget_switch3(id, value, options)
{
    var labels = options.split(',');
    var str = '<div class="switch3">';

    for (var i = 0; i < 3; i++) {
	var prefix = id+':switch3_'+i;
	str += '<label for="'+prefix+'" class="switch3_'+i+'_lbl">'+labels[i]+'</label>';
	str += '<input type="radio" value="'+i+'" name="'+id+':switch3" class="switch3_'+i+'" id="'+prefix+'"';
	str += ' onchange="hakit_widget_updated(this,this.value);"';
	if (i == value) {
	    str += ' checked';
	}
	str += '>';
    }

    str += '<div class="switch3_toggle"></div></div>';

    return str;
}


function hakit_widget_slider(id, value, options)
{
    var str = '<input type="range" class="slider" id="'+id+':value" onchange="hakit_widget_updated(this,this.value);"';

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


function hakit_widget_meter(id, value, options)
{
    var str = '<meter id="'+id+':value"';

    if (value) {
	str += ' value="'+value+'"';
    }

    if (options) {
        var list = options.split(',');
        for (var i = 0; i < list.length; i++) {
	    str += ' '+list[i];
        }
    }

    str += '>'+value+'</meter>';

    return str;
}


function hakit_widget_select(id, value, options)
{
    var str = '<select id="'+id+':value" onchange="hakit_widget_updated(this,this.value);">';

    if (options) {
        var list = options.split(',');
        for (var i = 0; i < list.length; i++) {
            var item = list[i];
	    str += '<option value="'+item+'"';
            if (item == value) {
                str += ' selected';
            }
            str += '>'+item+'</option>';
        }
    }

    str += '</select>';

    return str;
}


function hakit_widget_text(id, value, options)
{
    var str = '<textarea id="'+id+'"';

    if (options) {
        var list = options.split(',');
        for (var i = 0; i < list.length; i++) {
	    str += ' '+list[i];
        }
    }

    str += ' onchange="hakit_widget_updated(this,this.value);">';

    if (value) {
	str += value;
    }

    str += '</textarea>';

    return str;
}


function hakit_signal_clear()
{
    for (var tile_name in hakit_signals) {
        var rows = hakit_signals[tile_name].rows;
	while (rows.length > 1) {
	    rows.deleteRow(1);
	}
    }
}


window.onload = function()
{
    /* Detect browser */
    if (typeof BrowserDetect === "object") {
        BrowserDetect.init();
        document.getElementById("brow").textContent = " " + BrowserDetect.browser + " " + BrowserDetect.version +" " + BrowserDetect.OS +" ";
    }

    /* Show ws connection status */
    document.getElementById("ws_status_td").style.backgroundColor = "#ffff00";
    document.getElementById("ws_status").textContent = "Connecting...";

    /* Enable charts */
    hakit_chart_enable(document.getElementById("charts"));

    hakit_connect();
}


function hakit_signal_add(name, value, dir, widget)
{
    console.log('hakit_signal_add(name="'+name+'", value="'+value+'", dir='+dir+', widget='+widget+')');

    var valueStr = value;
    if (valueStr.length > 16) {
        valueStr = valueStr.substring(0,13)+"...";
    }

    var tab = name.split('.');
    var tile_name;
    var signal_name;
    if (tab.length > 1) {
        tile_name = tab[0];
        signal_name = tab[1];
    }
    else {
        tile_name = '';
        signal_name = tab[0];
    }

    var table_id = 'signal:'+tile_name;
    var table = document.getElementById(table_id);

    if (table == null) {
	console.log("hakit_signal_add: Create table "+table_id);

        var signals = document.getElementById("signals");
        var div = document.createElement('section');
        signals.appendChild(div);

        var str = '';
        if (tile_name != '') {
            str += '<h3>'+tile_name + '</h3>';
        }
        str += '<table class="pure-table">' +
            '<thead><tr><th>Direction</th><th>Signal</th><th>Value</th><th>Control</th></tr></thead>' +
            '<tbody id="'+table_id+'"></tbody>' +
            '</table>';
        div.innerHTML = str;

        table = document.getElementById(table_id);
        hakit_signals[tile_name] = table;
    }

    var len = table.rows.length;
    var row = table.insertRow(len);
    row.id = name;
    if (len % 2) {
	row.className = "pure-table-odd";
    }

    row.insertCell(0).innerHTML = dir;
    row.insertCell(1).innerHTML = signal_name;
    row.insertCell(2).innerHTML = valueStr;

    var args = widget.split(':');
    var wname = args[0];
    var options = args[1];

    var str = "";
    if (wname.substr(0,4) == 'led-') {
	str = hakit_widget_led(widget, value);
    }
    else if (wname == 'switch-slide') {
	str = hakit_widget_switch_slide(name, value);
    }
    else if (wname == 'switch-push') {
	str = hakit_widget_switch_push(name, value);
    }
    else if (wname == 'switch-3state') {
	str = hakit_widget_switch3(name, value, options);
    }
    else if (wname == 'meter') {
	str = hakit_widget_meter(name, value, options);
    }
    else if (wname == 'slider') {
	str = hakit_widget_slider(name, value, options);
    }
    else if (wname == 'select') {
	str = hakit_widget_select(name, value, options);
    }
    else if (wname == 'text') {
	str = hakit_widget_text(name, value, options);
    }

    row.insertCell(3).innerHTML = str;

    var cell = row.insertCell(4);
    cell.hidden = true;
    cell.innerHTML = widget;
}


function hakit_updated(name, value, dir, widget)
{
    console.log('hakit_updated(name="'+name+'", value="'+value+'")');

    var row = document.getElementById(name);
    if (row) {
        var valueStr = value;
        if (valueStr.length > 16) {
            valueStr = valueStr.substring(0,13)+"...";
        }

	// If row exists, update its value
	row.cells[2].innerHTML = valueStr;
	var widget = row.cells[4].innerHTML;

	if (widget.substr(0, 4) == 'led-') {
	    row.cells[3].innerHTML = hakit_widget_led(widget, value);
	}
	else if (widget.substr(0, 7) == 'switch-') {
            var args = widget.split(':');
            var wname = args[0];

	    if (wname == 'switch-3state') {
		var elmt = document.getElementById(name+':switch3_'+value);
		if (elmt) {
		    elmt.checked = 1;
		}
	    }
	    else {
		var elmt = document.getElementById(name+':switch');
		if (elmt) {
		    elmt.checked = ((value != '') && (value != '0'));
		}
	    }
	}
	else {
	    var elmt = document.getElementById(name+':value');
	    if (elmt) {
		elmt.value = value;
	    }
            else {
	        var elmt = document.getElementById(name);
	        if (elmt) {
		    elmt.innerHTML = value;
	        }
            }
	}
	return;
    }
    else {
	/* Append a new row */
	hakit_signal_add(name, value, dir, widget);
    }
}


function hakit_connected(connected)
{
    console.log("hakit_connected("+connected+")");

    if (connected) {
	document.getElementById("ws_status_td").style.backgroundColor = "#40ff40";
	document.getElementById("ws_status").textContent = "Connected";
	document.getElementById("ws_version").textContent = hakit_props['VERSION']+' '+hakit_props['ARCH'];
        var d = new Date(hakit_t0);
	document.getElementById("ws_t0").textContent = d.toDateString()+' '+d.toTimeString();
	hakit_signal_clear();
	hakit_chart_clear();
    }
    else {
	document.getElementById("ws_status_td").style.backgroundColor = "#ff4040";
	document.getElementById("ws_status").textContent = "Disconnected";
    }
}

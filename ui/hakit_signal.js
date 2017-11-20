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

var hakit_signals;

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
    var str = '<input type="range" class="slider" id="'+id+':slider" onchange="hakit_widget_updated(this,this.value);"';

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


function hakit_signal_clear()
{
    if (hakit_signals) {
	while (hakit_signals.rows.length > 1) {
	    hakit_signals.deleteRow(1);
	}
    }
}


function hakit_signal_init()
{
    document.getElementById("ws_status_td").style.backgroundColor = "#ffff00";
    document.getElementById("ws_status").textContent = "Connecting...";

    /* Get signal table element */
    hakit_signals = document.getElementById("signals");
    hakit_connect();
}


function hakit_signal_row(name)
{
    var rows = hakit_signals.rows;
    for (var i = 0; i < rows.length; i++) {
	var row = rows[i];
	if (row.cells[1].innerHTML == name) {
	    return row;
	}
    }

    return null;
}


function hakit_signal_add(name, value, dir, widget)
{
    console.log('hakit_signal_add(name="'+name+'", value="'+value+'", dir='+dir+', widget='+widget+')');

    var len = hakit_signals.rows.length;
    var row = hakit_signals.insertRow(len);
    if (len % 2) {
	row.className = "pure-table-odd";
    }

    row.insertCell(0).innerHTML = dir;
    row.insertCell(1).innerHTML = name;
    row.insertCell(2).innerHTML = value;

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

    row.insertCell(3).innerHTML = str;

    var cell = row.insertCell(4);
    cell.hidden = true;
    cell.innerHTML = widget;
}


function hakit_updated(name, value, dir, widget)
{
    console.log('hakit_updated(name="'+name+'", value="'+value+'")');

    var row = hakit_signal_row(name);
    if (row) {
	// If row exists, update its value
	row.cells[2].innerHTML = value;
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
	document.getElementById("ws_version").textContent = hakit_version;
	hakit_signal_clear();
    }
    else {
	document.getElementById("ws_status_td").style.backgroundColor = "#ff4040";
	document.getElementById("ws_status").textContent = "Disconnected";
    }
}

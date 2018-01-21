/*
 * HAKit - The Home Automation Kit
 * Copyright (C) 2014-2016 Sylvain Giroudon
 *
 * Signal charts
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

var hakit_chart = {
    container: undefined,
    list: {},
    signals: [],
};


function hakit_chart_enable(container)
{
    console.log("hakit_chart_enable()");
    hakit_chart.container = container;
}


function hakit_chart_enabled()
{
    return hakit_chart.container ? 1:0;
}


function hakit_chart_clear()
{
    console.log("hakit_chart_clear()");

    for (var chart_name in hakit_chart.list) {
        var chart = hakit_chart.list[chart_name];
        chart.chart.destroy();
        chart.config = {};
        chart.signals.length = 0;
    }

    hakit_chart.list = {};
    hakit_chart.signals.length = 0;
}


function hakit_chart_init()
{
    var COLORS = [
        '#4dc9f6',
        '#f67019',
        '#f53794',
        '#537bc4',
        '#acc236',
        '#166a8f',
        '#00a950',
        '#58595b',
        '#8549ba'
    ];

    if (!hakit_chart.container) {
        return;
    }

    console.log("hakit_chart_init()");

    for (var chart_name in hakit_chart.list) {
        var chart = hakit_chart.list[chart_name];
        var color_index = 0;

        var canvas_id = 'hakit_chart:'+chart_name+':canvas';
	var canvas = document.getElementById(canvas_id);
        if (!canvas) {
            canvas = document.createElement('canvas');
            canvas.setAttribute('id', canvas_id);
            hakit_chart.container.appendChild(canvas);
        }

        chart.config = {
	    type: 'line',
	    data: {
	        datasets: []
	    },
	    options: {
	        responsive: true,
	        title: {
		    display: true,
		    text: chart_name,
	        },
                scales: {
		    xAxes: [{
			type: "time",
			time: {
			    displayFormats: {
                                millisecond: 'HH:mm:ss.SSS',
                                second: 'HH:mm:ss',
                                minute: 'HH:mm',
                                hour: 'HH',
                            },
			    tooltipFormat: 'll HH:mm:ss.SSS'
			},
			scaleLabel: {
			    display: true,
			    //labelString: 'Date'
			}
		    }, ],
		    yAxes: [],
		},
	    }
        };

        var yaxis_shown = {};
        var yaxis_count = 0;

        for (var i=0; i < chart.signals.length; i++) {
            var signal = chart.signals[i];
            var dataset = {
                label: signal.name,
		steppedLine: true,
		data: [],
		fill: false,
                yAxisID: signal.yaxis
            };
            if (signal.color) {
                dataset.borderColor = signal.color;
            }
            else {
                dataset.borderColor = COLORS[color_index % COLORS.length];
                color_index++;
            }
            chart.config.data.datasets.push(dataset);
            hakit_chart.signals[signal.name] = {
                chart: chart,
                dataset: dataset,
            };

            if (signal.yaxis) {
                if (!yaxis_shown[signal.yaxis]) {
                    position = (yaxis_count > 0) ? 'right':'left';

                    var yaxis = {
			display: true,
                        position: position,
			id: signal.yaxis,
			scaleLabel: {
			    display: true,
			    labelString: signal.yaxis,
			}
                    };
                    chart.config.options.scales.yAxes.push(yaxis);

                    yaxis_shown[signal.yaxis] = 1;
                    yaxis_count++;
                }
            }
        }

        var ctx = canvas.getContext('2d');
        chart.chart = new Chart(ctx, chart.config);
    }
}


function hakit_chart_add(chart_spec, signal_spec, signal_color)
{
    if (!hakit_chart.container) {
        return;
    }

    console.log('hakit_chart_add(chart_spec="'+chart_spec+'", signal_spec="'+signal_spec+'", signal_color="'+signal_color+'")');

    // Split chart name from Y axis name
    var tab = chart_spec.split('/');
    var chart_name = tab[0];
    var yaxis = tab[1];

    // Create chart if it does not already exist
    var chart = hakit_chart.list[chart_name];
    if (!chart) {
        chart = {
            signals: [],
            config: undefined,
            chart: undefined,
        };
        hakit_chart.list[chart_name] = chart;
    }

    // Extract signal name from full signal spec
    var signal_name = signal_spec.split(".").pop();

    // Ignore signal if it already has a chart
    for (var i = 0; i < chart.signals.length; i++) {
        if (chart.signals[i].name == signal_name) {
            return;
        }
    }

    // Add new signal to chart
    var signal = {
        name: signal_name,
        color: '',
        yaxis: yaxis,
        rank: chart.signals.length,
    };
    if (signal_color) {
        signal.color = signal_color;
    }
    chart.signals.push(signal);
}


function hakit_chart_set(signal_name, data)
{
    var signal = hakit_chart.signals[signal_name];
    if (signal) {
        signal.dataset.data = data;
        hakit_chart_cut_first(signal.chart);
        signal.chart.chart.update();
    }
}


function hakit_chart_cut_first(chart)
{
    // Get earliest time stamp of the first trace
    var signal0 = hakit_chart.signals[chart.signals[0].name];
    var t0 = signal0.dataset.data[0].t;

    // Clip all other traces from this time stamp
    for (var i = 1; i < chart.signals.length; i++) {
        var signal = hakit_chart.signals[chart.signals[i].name];
        var iprev = -1;
        for (var j = 0; j < signal.dataset.data.length; j++) {
            if (signal.dataset.data[j].t >= t0) {
                break;
            }
            iprev = j;
        }
        if (iprev >= 0) {
            signal.dataset.data[iprev].t = t0;
            if (iprev > 0) {
                signal.dataset.data.splice(0,iprev);
            }
        }
    }
}

function hakit_chart_ext_remove(signal)
{
    if (signal.dataset.data.length > 0) {
        var pt = signal.dataset.data[signal.dataset.data.length-1];
        if (pt.ext) {
            signal.dataset.data.pop();
        }
    }
}


function hakit_chart_ext(signal)
{
    var chart = signal.chart;
    var pt = signal.dataset.data[signal.dataset.data.length-1];

    // Extend all traces to the latest time stamp
    for (var i = 0; i < chart.signals.length; i++) {
        var signal1 = hakit_chart.signals[chart.signals[i].name];
        if (signal1 != signal) {
            if (signal1.dataset.data.length > 0) {
                var pt1 = signal1.dataset.data[signal1.dataset.data.length-1];
                if (pt1.t < pt.t) {
                    if (pt1.ext) {
                        pt1.t = pt.t;
                    }
                    else {
                        var pt2 = {
                            t: pt.t,
                            y: pt1.y,
                            ext: true,
                        };
                        signal1.dataset.data.push(pt2);
                    }
                }
            }
        }
    }
}


function hakit_chart_updated(signal_spec, pt)
{
    // Extract signal name from full signal spec
    var signal_name = signal_spec.split(".").pop();

    var signal = hakit_chart.signals[signal_name];
    if (signal) {
        hakit_chart_ext_remove(signal);

        if (signal.dataset.data.length >= 500) {
            signal.dataset.data.shift();
        }
        signal.dataset.data.push(pt);

        // Clamp traces if chart has multiple traces
        if (signal.chart.signals.length > 1) {
            hakit_chart_cut_first(signal.chart);
            hakit_chart_ext(signal);
        }

        signal.chart.chart.update();
    }
}

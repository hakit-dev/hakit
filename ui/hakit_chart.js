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

var hakit_charts = {};
var hakit_chart_signals = [];
var hakit_chart_labels = [];


function hakit_chart_init(container)
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

    for (var chart_name in hakit_charts) {
        var chart = hakit_charts[chart_name];
        var color_index = 0;

        var canvas = document.createElement('canvas');
        container.appendChild(canvas);

        chart.config = {
	    type: 'line',
	    data: {
	        labels: [],
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
			    format: 'MM/DD/YYYY HH:mm:ss',
			    // round: 'day'
			    tooltipFormat: 'll HH:mm:ss'
			},
			scaleLabel: {
			    display: true,
			    labelString: 'Date'
			}
		    }, ],
		    yAxes: [{
			scaleLabel: {
			    display: true,
			    labelString: 'Value'
			}
		    }]
		},
	    }
        };

        for (var i=0; i < chart.signals.length; i++) {
            var signal = chart.signals[i];
            var dataset = {
                label: signal.name,
		steppedLine: true,
		data: [],
		fill: false,
            };
            if (signal.color) {
                dataset.borderColor = signal.color;
            }
            else {
                dataset.borderColor = COLORS[color_index % COLORS.length];
                color_index++;
            }
            chart.config.data.datasets.push(dataset);
            hakit_chart_signals[signal.name] = {
                chart: chart,
                dataset: dataset,
            };
        }

        var ctx = canvas.getContext('2d');
        chart.chart = new Chart(ctx, chart.config);
    }
}


function hakit_chart_add(chart_name, signal_spec, signal_color)
{
    // Create chart if it does not already exist
    if (!hakit_charts[chart_name]) {
        hakit_charts[chart_name] = {
            signals: [],
            config: undefined,
            chart: undefined,
        };
    }

    var chart = hakit_charts[chart_name];

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
    };
    if (signal_color) {
        signal.color = signal_color;
    }
    chart.signals.push(signal);
}


function hakit_chart_update(name, data)
{
    if (hakit_chart_signals[name]) {
        var chart = hakit_chart_signals[name].chart;

        for (var i = 0; i < data.length; i++) {
            var t = data[i].t;
            if ((hakit_chart_labels.length <= 0) || (t > hakit_chart_labels[hakit_chart_labels.length-1])) {
                hakit_chart_labels.push(t);
            }
        }
            
        chart.config.data.labels = hakit_chart_labels;
        hakit_chart_signals[name].dataset.data = data;

        chart.chart.update();
    }
}

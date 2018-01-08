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

    var div = document.createElement('div');
    div.classList.add('chart-container');
    container.appendChild(div);

    for (var chart_name in hakit_charts) {
        var chart = hakit_charts[chart_name];
        var color_index = 0;

        var canvas = document.createElement('canvas');
        div.appendChild(canvas);

        var ctx = canvas.getContext('2d');
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
			    labelString: 'value'
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

        chart.chart = new Chart(ctx, chart.config);
    }
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

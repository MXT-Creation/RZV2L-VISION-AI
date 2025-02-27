let telemetrySocket = new_ws("telemetry");

let graphCtx = document.getElementById('graph_canvas').getContext('2d');

let graphChart = new Chart(graphCtx, {
  type: 'line',
  data: {
    labels: [],
    datasets: []
  },
  options: {
    scales: {
      y: {
        type: 'linear',
        min: 0,
        max: 100
      }
    },
    animation: {
      duration: 0
    }
  }
});

telemetrySocket.onmessage = function(event) {
    let data = JSON.parse(event.data);
    if (data.type === "cpu_usage") {
        if (graphChart.data.datasets.length === 0) {
            initializeDatasets(data.data.length);
        }
        updateGraph(data.data);
    } else if (data.type === "proc_time") {
        updateProcTimeDisplay(data.data);
    }
};

function updateProcTimeDisplay(procTimeData) {
    let drpWindow = document.getElementById('drp_window');
    if (!drpWindow) return;
    
    let displayText = 
        `Inference time (DRP-AI):\t${procTimeData.inference.toFixed(2)} ms\n` +
        `Post-processing time (CPU):\t${procTimeData.post_processing.toFixed(2)} ms\n` +
        `Total processing time:\t${(procTimeData.inference + procTimeData.post_processing).toFixed(2)} ms`;
    
    drpWindow.value = displayText;
}

function initializeDatasets(numCores) {
    for (let i = 0; i < numCores; i++) {
        graphChart.data.datasets.push({
            label: `CPU ${i} Usage[%]`,
            data: [],
            borderColor: getColor(i),
            backgroundColor: 'rgba(0,0,0,0)'
        });
    }
}

function getColor(index) {
    const colors = ['blue', 'deepskyblue', 'green', 'red', 'orange', 'purple', 'yellow', 'pink'];
    return colors[index % colors.length];
}

function updateGraph(cpuData) {
    let labels = graphChart.data.labels;
    let datasets = graphChart.data.datasets;

    if (labels.length > 20) {
        labels.shift();
        datasets.forEach(dataset => dataset.data.shift());
    }

    labels.push(new Date().toLocaleTimeString());
    cpuData.forEach((usage, index) => {
        datasets[index].data.push(usage);
    });

    graphChart.update();
}

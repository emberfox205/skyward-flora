/* eslint-disable max-classes-per-file */
/* eslint-disable no-restricted-globals */
/* eslint-disable no-undef */
$(document).ready(() => {
  // if deployed to a site supporting SSL, use wss://
  const protocol = document.location.protocol.startsWith('https') ? 'wss://' : 'ws://';
  const wsHost = location.hostname || 'localhost';
  const wsPort = location.port;
  const webSocket = new WebSocket(`${protocol}${wsHost}${wsPort ? ':' + wsPort : ''}/ws`);

  // A class for holding the last N points of telemetry for a device
  class DeviceData {
    constructor(deviceId) {
      this.deviceId = deviceId;
      this.maxLen = 50;
      this.timeData = new Array(this.maxLen);
      this.temperatureData = new Array(this.maxLen);
      this.humidityData = new Array(this.maxLen);
      this.soilMoistureData = new Array(this.maxLen);
    }

    addData(time, temperature, humidity, soilMoisture) {
      // Convert ISO timestamp string to Date object
      const timeStamp = new Date(time);
      
      this.timeData.push(timeStamp);
      
      // Use nullish coalescing to only convert undefined or null to null (preserves 0)
      this.temperatureData.push(temperature ?? null);
      this.humidityData.push(humidity ?? null);
      this.soilMoistureData.push(soilMoisture ?? null);
    
      if (this.timeData.length > this.maxLen) {
        this.timeData.shift();
        this.temperatureData.shift();
        this.humidityData.shift();
        this.soilMoistureData.shift();
      }
    }
  }

  // All the devices in the list (those that have been sending telemetry)
  class TrackedDevices {
    constructor() {
      this.devices = [];
    }

    // Find a device based on its Id
    findDevice(deviceId) {
      for (let i = 0; i < this.devices.length; ++i) {
        if (this.devices[i].deviceId === deviceId) {
          return this.devices[i];
        }
      }

      return undefined;
    }

    getDevicesCount() {
      return this.devices.length;
    }
  }

  const trackedDevices = new TrackedDevices();

  // Define chart configurations
  
  // Temperature Chart
  const tempChartConfig = {
    type: 'line',
    data: {
      datasets: [{
        fill: false,
        label: 'Temperature',
        borderColor: 'rgba(255, 99, 132, 1)',
        backgroundColor: 'rgba(255, 99, 132, 0.2)',
        borderWidth: 2,
        pointRadius: 3,
        spanGaps: true,
      }]
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      scales: {
        xAxes: [{
          type: 'category',  // Changed from 'time' to 'category'
          display: true,
          scaleLabel: {
            display: true,
            labelString: 'Data Points'  // Changed from 'Time'
          },
          ticks: {
            autoSkip: true,
            maxTicksLimit: 10,
            callback: function(value, index) {
              // Show data point number or simplified timestamp
              return index + 1;
            }
          }
        }],
        yAxes: [{
          display: true,
          scaleLabel: {
            display: true,
            labelString: 'Temperature (°C)'
          },
          ticks: {
            beginAtZero: true,
            suggestedMax: 40
          }
        }]
      }
    }
  };

  // Humidity Chart
  const humidityChartConfig = {
    type: 'line',
    data: {
      datasets: [{
        fill: false,
        label: 'Humidity',
        borderColor: 'rgba(54, 162, 235, 1)',
        backgroundColor: 'rgba(54, 162, 235, 0.2)',
        borderWidth: 2,
        pointRadius: 3,
        spanGaps: true,
      }]
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      scales: {
        xAxes: [{
          type: 'category',  // Changed from 'time' to 'category'
          display: true,
          scaleLabel: {
            display: true,
            labelString: 'Data Points'  // Changed from 'Time'
          },
          ticks: {
            autoSkip: true,
            maxTicksLimit: 10,
            callback: function(value, index) {
              // Show data point number or simplified timestamp
              return index + 1;
            }
          }
        }],
        yAxes: [{
          display: true,
          scaleLabel: {
            display: true,
            labelString: 'Humidity (%)'
          },
          ticks: {
            beginAtZero: true,
            suggestedMax: 100
          }
        }]
      }
    }
  };

  // Soil Moisture Chart
  const soilMoistureChartConfig = {
    type: 'line',
    data: {
      datasets: [{
        fill: false,
        label: 'Soil Moisture',
        borderColor: 'rgba(75, 192, 192, 1)',
        backgroundColor: 'rgba(75, 192, 192, 0.2)',
        borderWidth: 2,
        pointRadius: 3,
        spanGaps: true,
      }]
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      scales: {
        xAxes: [{
          type: 'category',  // Changed from 'time' to 'category'
          display: true,
          scaleLabel: {
            display: true,
            labelString: 'Data Points'  // Changed from 'Time'
          },
          ticks: {
            autoSkip: true,
            maxTicksLimit: 10,
            callback: function(value, index) {
              // Show data point number or simplified timestamp
              return index + 1;
            }
          }
        }],
        yAxes: [{
          display: true,
          scaleLabel: {
            display: true,
            labelString: 'Soil Moisture (%)'
          },
          ticks: {
            beginAtZero: true,
            suggestedMax: 100
          }
        }]
      }
    }
  };

  // Create the charts
  const tempCtx = document.getElementById('tempChart').getContext('2d');
  const humidityCtx = document.getElementById('humidityChart').getContext('2d');
  const soilMoistureCtx = document.getElementById('soilMoistureChart').getContext('2d');
  
  const tempChart = new Chart(tempCtx, tempChartConfig);
  const humidityChart = new Chart(humidityCtx, humidityChartConfig);
  const soilMoistureChart = new Chart(soilMoistureCtx, soilMoistureChartConfig);

  // Get references to the real-time display elements
  const tempValue = document.getElementById('tempValue');
  const humidityValue = document.getElementById('humidityValue');
  const soilMoistureValue = document.getElementById('soilMoistureValue');
  const tempTime = document.getElementById('tempTime');
  const humidityTime = document.getElementById('humidityTime');
  const soilMoistureTime = document.getElementById('soilMoistureTime');

  // Manage a list of devices in the UI, and update which device data the chart is showing
  let needsAutoSelect = true;
  const deviceCount = document.getElementById('deviceCount');
  const listOfDevices = document.getElementById('listOfDevices');
  
  function formatTime(time) {
    const date = new Date(time);
    return date.toLocaleTimeString();
  }
  
  function updateCharts() {
    const device = trackedDevices.findDevice(listOfDevices[listOfDevices.selectedIndex].text);
    if (!device) return;
    
    // Create array of point labels
    const pointLabels = Array.from({length: device.timeData.length}, (_, i) => `Point ${i+1}`);
    
    // Update temperature chart with evenly-spaced points
    tempChart.data.labels = pointLabels;
    tempChart.data.datasets[0].data = device.temperatureData;
    
    // Add time information to tooltip
    tempChart.options.tooltips = {
      callbacks: {
        title: function(tooltipItem) {
          const index = tooltipItem[0].index;
          const time = device.timeData[index];
          return time ? new Date(time).toLocaleString() : '';
        }
      }
    };
    tempChart.update();
    
    // Repeat for humidity chart
    humidityChart.data.labels = pointLabels;
    humidityChart.data.datasets[0].data = device.humidityData;
    humidityChart.options.tooltips = {...tempChart.options.tooltips};
    humidityChart.update();
    
    // Repeat for soil moisture chart
    soilMoistureChart.data.labels = pointLabels;
    soilMoistureChart.data.datasets[0].data = device.soilMoistureData;
    soilMoistureChart.options.tooltips = {...tempChart.options.tooltips};
    soilMoistureChart.update();
    
    // Update real-time values if data exists
    if (device.temperatureData.length > 0) {
      const lastTempIndex = device.temperatureData.length - 1;
      const lastTemp = device.temperatureData[lastTempIndex];
      if (lastTemp !== null) {
        tempValue.innerText = `${lastTemp.toFixed(1)} °C`;
        tempTime.innerText = `Last update: ${formatTime(device.timeData[lastTempIndex])}`;
      }
    }
    
    if (device.humidityData.length > 0) {
      const lastHumIndex = device.humidityData.length - 1;
      const lastHum = device.humidityData[lastHumIndex];
      if (lastHum !== null) {
        humidityValue.innerText = `${lastHum.toFixed(1)} %`;
        humidityTime.innerText = `Last update: ${formatTime(device.timeData[lastHumIndex])}`;
      }
    }
    
    if (device.soilMoistureData.length > 0) {
      const lastSoilIndex = device.soilMoistureData.length - 1;
      const lastSoil = device.soilMoistureData[lastSoilIndex];
      if (lastSoil !== null) {
        soilMoistureValue.innerText = `${lastSoil.toFixed(1)} %`;
        soilMoistureTime.innerText = `Last update: ${formatTime(device.timeData[lastSoilIndex])}`;
      }
    }
  }
  
  listOfDevices.addEventListener('change', updateCharts, false);

  // When a web socket message arrives:
  // 1. Unpack it
  // 2. Validate it has date/time and at least one sensor value
  // 3. Find or create a cached device to hold the telemetry data
  // 4. Append the telemetry data
  // 5. Update the chart UI
  webSocket.onmessage = function onMessage(message) {
    try {
      const messageData = JSON.parse(message.data);
      console.log(messageData);

      // Correctly check for undefined/null (not falsy values)
      if (messageData.MessageDate === undefined || messageData.MessageDate === null || 
          (messageData.IotData.temp === undefined || messageData.IotData.temp === null) && 
          (messageData.IotData.humidity === undefined || messageData.IotData.humidity === null) && 
          (messageData.IotData.soil_moisture === undefined || messageData.IotData.soil_moisture === null)) {
        return;
      }

      // find or add device to list of tracked devices
      const existingDeviceData = trackedDevices.findDevice(messageData.DeviceId);

      if (existingDeviceData) {
        existingDeviceData.addData(
          messageData.MessageDate, 
          messageData.IotData.temp, 
          messageData.IotData.humidity,
          messageData.IotData.soil_moisture
        );
      } else {
        const newDeviceData = new DeviceData(messageData.DeviceId);
        trackedDevices.devices.push(newDeviceData);
        const numDevices = trackedDevices.getDevicesCount();
        deviceCount.innerText = numDevices === 1 ? `${numDevices} device` : `${numDevices} devices`;
        newDeviceData.addData(
          messageData.MessageDate, 
          messageData.IotData.temp, 
          messageData.IotData.humidity,
          messageData.IotData.soil_moisture
        );

        // add device to the UI list
        const node = document.createElement('option');
        const nodeText = document.createTextNode(messageData.DeviceId);
        node.appendChild(nodeText);
        listOfDevices.appendChild(node);

        // if this is the first device being discovered, auto-select it
        if (needsAutoSelect) {
          needsAutoSelect = false;
          listOfDevices.selectedIndex = 0;
          updateCharts();
        }
      }

      updateCharts();
    } catch (err) {
      console.error(err);
    }
  };
});

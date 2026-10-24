let isDemo = false;
let espIp = localStorage.getItem("saved_esp_ip") || "smartgarden.local";
let pollTimer = null;
let chart = null;

let lastPumpState = false;
let lastWaterState = true;
let logHistory = []; // Lưu trữ lịch sử sự kiện

// Khởi tạo Biểu đồ Chart.js
function initChart() {
  const ctx = document.getElementById('realtimeChart').getContext('2d');
  chart = new Chart(ctx, {
    type: 'line',
    data: {
      labels: [],
      datasets: [
        {
          label: 'Độ ẩm đất (%)',
          borderColor: '#10b981',
          backgroundColor: 'rgba(16, 185, 129, 0.1)',
          fill: true,
          tension: 0.3,
          data: []
        },
        {
          label: 'Nhiệt độ (°C)',
          borderColor: '#f59e0b',
          backgroundColor: 'transparent',
          borderDash: [5, 5],
          tension: 0.3,
          data: []
        }
      ]
    },
    options: {
      responsive: true,
      animation: false,
      scales: {
        y: {
          min: 0,
          max: 100,
          ticks: { stepSize: 20 }
        }
      }
    }
  });
}

function addDataToChart(timeStr, soil, temp) {
  if (!chart) return;
  if (chart.data.labels.length > 25) {
    chart.data.labels.shift();
    chart.data.datasets[0].data.shift();
    chart.data.datasets[1].data.shift();
  }
  chart.data.labels.push(timeStr);
  chart.data.datasets[0].data.push(soil);
  chart.data.datasets[1].data.push(temp);
  chart.update();
}

// Cập nhật giao diện với dữ liệu từ ESP32
function updateUI(data) {
  document.getElementById('statusDot').className = 'dot connected';
  document.getElementById('statusText').innerText = 'Đã kết nối với ESP32';
  document.getElementById('statusText').style.color = '#10b981';

  // Đồng hồ thực
  if (data.time && data.time !== "--:--:--") {
    document.getElementById('clockBox').innerText = '🕒 ' + data.time;
  }

  // Cảm biến
  document.getElementById('valTemp').innerText = data.temp + ' °C';
  document.getElementById('valHum').innerText = data.hum + ' %';
  document.getElementById('valSoil').innerText = data.soil + ' %';
  document.getElementById('valRaw').innerText = 'Tín hiệu ADC thô: ' + data.raw;

  // Trạng thái cảm biến đo mực nước bình chứa (%)
  const waterValEl = document.getElementById('valWater');
  const waterSubEl = document.getElementById('waterSub');
  const waterBar = document.getElementById('waterBarFill');
  const banner = document.getElementById('safetyAlertBanner');
  const alertMsg = document.getElementById('safetyAlertMsg');

  const waterPct = data.water_pct !== undefined ? data.water_pct : (data.water ? 80 : 5);
  waterValEl.innerText = waterPct + ' %';
  waterBar.style.width = waterPct + '%';

  if (waterPct < 15 || !data.water) {
    waterBar.style.backgroundColor = '#ef4444';
    waterSubEl.innerHTML = '<span class="badge-water water-empty">CẠN NƯỚC (KHÓA BƠM)</span>';
    banner.style.display = 'block';
    alertMsg.innerText = 'CẢNH BÁO: BÌNH CHỨA ĐÃ HẾT NƯỚC (<15%)! HỆ THỐNG ĐÃ TỰ ĐỘNG KHÓA BƠM ĐỂ BẢO VỆ.';
  } else if (waterPct < 35) {
    waterBar.style.backgroundColor = '#f59e0b';
    waterSubEl.innerHTML = '<span class="badge-water" style="background:#fef3c7; color:#b45309;">SẮP HẾT NƯỚC</span>';
    if (!data.timeout_alert) banner.style.display = 'none';
  } else {
    waterBar.style.backgroundColor = '#3b82f6';
    waterSubEl.innerHTML = '<span class="badge-water water-ok">NƯỚC ĐẦY ĐỦ</span>';
    if (!data.timeout_alert) banner.style.display = 'none';
  }

  // Cảnh báo timeout
  if (data.timeout_alert) {
    banner.style.display = 'block';
    alertMsg.innerText = 'CẢNH BÁO: MÁY BƠM ĐÃ CHẠY QUÁ 60 GIÂY LIÊN TỤC VÀ ĐÃ ĐƯỢC TỰ ĐỘNG NGẮT AN TOÀN!';
  }

  // Trạng thái Chế độ Mode
  const modeBtn = document.getElementById('modeBtn');
  if (data.mode === "AUTO") {
    modeBtn.className = 'btn-mode mode-auto';
    modeBtn.innerText = 'TỰ ĐỘNG [AUTO]';
  } else {
    modeBtn.className = 'btn-mode mode-manu';
    modeBtn.innerText = 'THỦ CÔNG [MANU]';
  }

  // Máy bơm & Thời gian chạy
  const pumpBtn = document.getElementById('mainPumpBtn');
  const pumpLabel = document.getElementById('pumpStatusLabel');
  document.getElementById('pumpDuration').innerText = data.pump_time || 0;

  if (data.pump) {
    pumpBtn.className = 'btn-pump btn-pump-on';
    pumpBtn.innerText = 'TẮT MÁY BƠM NGAY';
    pumpLabel.innerText = 'ĐANG HOẠT ĐỘNG (BẬT)';
    pumpLabel.style.color = '#ef4444';
  } else {
    pumpBtn.className = 'btn-pump btn-pump-off';
    pumpBtn.innerText = 'BẬT MÁY BƠM';
    pumpLabel.innerText = 'ĐANG DỪNG (TẮT)';
    pumpLabel.style.color = '#10b981';
  }

  // Ghi nhật ký sự kiện nếu trạng thái thay đổi
  if (data.pump !== lastPumpState) {
    const timeNow = data.time || new Date().toLocaleTimeString();
    if (data.pump) {
      addLogEntry(timeNow, 'Bật máy bơm', data.soil + '%', data.temp + '°C', data.mode === "AUTO" ? 'Tự động (Đất khô)' : 'Thủ công (Nút bấm)');
    } else {
      addLogEntry(timeNow, 'Tắt máy bơm', data.soil + '%', data.temp + '°C', 'Bơm hoàn tất');
    }
    lastPumpState = data.pump;
  }

  if (data.water !== lastWaterState) {
    const timeNow = data.time || new Date().toLocaleTimeString();
    if (!data.water) {
      addLogEntry(timeNow, 'Hết nước bình chứa', data.soil + '%', data.temp + '°C', 'Phao D19 kích hoạt');
    }
    lastWaterState = data.water;
  }

  // Cập nhật biểu đồ
  const timeLabel = data.time !== "--:--:--" ? data.time : new Date().toLocaleTimeString();
  addDataToChart(timeLabel, data.soil, data.temp);
}

// Thêm dòng vào bảng nhật ký
function addLogEntry(time, event, soil, temp, note) {
  const tbody = document.getElementById('logTableBody');
  const row = document.createElement('tr');

  let badgeClass = 'log-auto';
  if (event.includes('Hết nước')) badgeClass = 'log-alert';
  else if (note.includes('Thủ công')) badgeClass = 'log-manual';

  row.innerHTML = `
    <td>${time}</td>
    <td><span class="log-badge ${badgeClass}">${event}</span></td>
    <td>${soil}</td>
    <td>${temp}</td>
    <td>${note}</td>
  `;

  tbody.insertBefore(row, tbody.firstChild);

  // Lưu vào mảng để xuất CSV
  logHistory.push({ time, event, soil, temp, note });
}

// Gọi API lấy dữ liệu từ ESP32
function fetchData() {
  if (isDemo) {
    simulateData();
    return;
  }

  espIp = document.getElementById('espIp').value.trim();
  localStorage.setItem("saved_esp_ip", espIp);

  fetch(`http://${espIp}/api/data`, { method: 'GET', mode: 'cors' })
    .then(res => {
      if (!res.ok) throw new Error("Network error");
      return res.json();
    })
    .then(data => updateUI(data))
    .catch(err => {
      document.getElementById('statusDot').className = 'dot disconnected';
      document.getElementById('statusText').innerText = `Mất kết nối với ESP32 (${espIp})`;
      document.getElementById('statusText').style.color = '#ef4444';
    });
}

// Bật / tắt máy bơm
function togglePump() {
  if (isDemo) {
    window.demoPump = !window.demoPump;
    return;
  }
  espIp = document.getElementById('espIp').value.trim();
  fetch(`http://${espIp}/api/toggle`, { method: 'GET', mode: 'cors' })
    .then(() => fetchData())
    .catch(err => alert("Lỗi gửi lệnh tới ESP32!"));
}

// Đổi chế độ Auto / Manual
function toggleMode() {
  if (isDemo) {
    window.demoMode = window.demoMode === "AUTO" ? "MANU" : "AUTO";
    return;
  }
  espIp = document.getElementById('espIp').value.trim();
  fetch(`http://${espIp}/api/mode`, { method: 'GET', mode: 'cors' })
    .then(() => fetchData())
    .catch(err => alert("Lỗi đổi chế độ!"));
}

// Thay đổi số hiển thị khi kéo thanh trượt
function onSliderChange() {
  document.getElementById('lblDry').innerText = document.getElementById('sliderDry').value + '%';
  document.getElementById('lblWet').innerText = document.getElementById('sliderWet').value + '%';
}

// Lưu ngưỡng độ ẩm mới vào Flash ESP32
function saveThresholds() {
  const dry = document.getElementById('sliderDry').value;
  const wet = document.getElementById('sliderWet').value;

  if (parseInt(dry) >= parseInt(wet)) {
    alert("Ngưỡng đất khô phải nhỏ hơn ngưỡng đủ ẩm!");
    return;
  }

  if (isDemo) {
    alert("Đã lưu ngưỡng giả lập: Bật < " + dry + "%, Tắt >= " + wet + "%");
    return;
  }

  espIp = document.getElementById('espIp').value.trim();
  fetch(`http://${espIp}/api/settings?dry=${dry}&wet=${wet}`, { method: 'GET', mode: 'cors' })
    .then(res => res.json())
    .then(data => alert(`Đã lưu thành công vào Flash ESP32!\nBật bơm khi < ${dry}%\nTắt bơm khi >= ${wet}%`))
    .catch(err => alert("Lỗi khi lưu vào ESP32!"));
}

// Xuất file báo cáo Excel (CSV)
function exportCSV() {
  if (logHistory.length === 0) {
    alert("Chưa có dữ liệu nhật ký nào để xuất!");
    return;
  }

  let csvContent = "data:text/csv;charset=utf-8,\uFEFF";
  csvContent += "Thoi Gian,Su Kien,Do Am Dat,Nhiet Do,Ghi Chu\n";

  logHistory.forEach(item => {
    csvContent += `"${item.time}","${item.event}","${item.soil}","${item.temp}","${item.note}"\n`;
  });

  const encodedUri = encodeURI(csvContent);
  const link = document.createElement("a");
  link.setAttribute("href", encodedUri);
  link.setAttribute("download", `Bao_Cao_Tuoi_ESP32_${new Date().toISOString().slice(0, 10)}.csv`);
  document.body.appendChild(link);
  link.click();
  document.body.removeChild(link);
}

function reconnect() {
  fetchData();
}

// Chế độ giả lập Demo
window.demoPump = false;
window.demoMode = "AUTO";
function toggleDemo() {
  isDemo = !isDemo;
  const btn = document.getElementById('demoBtn');
  if (isDemo) {
    btn.innerText = 'Chế độ Demo: ĐANG BẬT';
    btn.style.background = '#10b981';
    btn.style.color = 'white';
  } else {
    btn.innerText = 'Chế độ Demo: TẮT';
    btn.style.background = '#e2e8f0';
    btn.style.color = '#475569';
  }
}

function simulateData() {
  const fakeData = {
    temp: (29.2 + Math.random() * 1.5).toFixed(1),
    hum: (64 + Math.random() * 4).toFixed(1),
    soil: Math.floor(42 + Math.random() * 8),
    raw: Math.floor(2250 + Math.random() * 100),
    touch: Math.random() > 0.85,
    pump: window.demoPump,
    mode: window.demoMode,
    water: true,
    water_pct: Math.floor(75 + Math.random() * 5),
    water_raw: 2100,
    timeout_alert: false,
    pump_time: window.demoPump ? 15 : 0,
    time: new Date().toLocaleTimeString()
  };
  updateUI(fakeData);
}

// Khởi chạy
window.onload = function() {
  document.getElementById('espIp').value = espIp;
  initChart();
  fetchData();
  pollTimer = setInterval(fetchData, 1500);
};

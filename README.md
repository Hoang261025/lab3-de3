# lab3-de3 Mạch báo cháy gia đình bằng việc phát hiện khí gas/khói và nhiệt độ.
# 🔥 ESP32 Smart Fire Alarm System – IoT & FreeRTOS
link wokki : [mô phỏng trên wokki](https://wokwi.com/projects/480202555320653825)
## 1. Giới thiệu dự án

**ESP32 Smart Fire Alarm System** là hệ thống cảnh báo cháy thông minh ứng dụng công nghệ IoT, sử dụng vi điều khiển ESP32 để giám sát nồng độ khí gas/khói và nhiệt độ môi trường.

Hệ thống thu thập dữ liệu từ cảm biến MQ-2 và DHT22, xử lý song song bằng **FreeRTOS**, đồng thời gửi dữ liệu lên MQTT Broker thông qua Wi-Fi. Người dùng có thể theo dõi các thông số cảm biến và điều khiển thiết bị từ xa thông qua giao diện Web.

Khi phát hiện nồng độ khí gas hoặc nhiệt độ vượt ngưỡng cài đặt, hệ thống tự động kích hoạt thiết bị cảnh báo và cơ cấu chấp hành.

### Mục tiêu dự án

- Giám sát nồng độ khí gas/khói và nhiệt độ theo thời gian thực.
- Phát hiện các điều kiện bất thường và đưa ra cảnh báo.
- Ứng dụng FreeRTOS để quản lý các tác vụ độc lập.
- Truyền dữ liệu cảm biến qua giao thức MQTT.
- Xây dựng giao diện Web để giám sát và điều khiển thiết bị từ xa.

## 2. Kiến trúc hệ thống

```text
             ┌─────────────────────┐
             │      MQ-2           │
             │ Gas / Smoke Sensor  │
             └──────────┬──────────┘
                        │
             ┌──────────▼──────────┐
             │                     │
             │        ESP32        │
             │                     │
             │     FreeRTOS        │
             │                     │
             │  ┌───────────────┐  │
             │  │   Task Read   │  │
             │  │  Core 1       │  │
             │  └───────┬───────┘  │
             │          │ Queue    │
             │  ┌───────▼───────┐  │
             │  │  Task Alert   │  │
             │  │  Core 1       │  │
             │  └───────────────┘  │
             │          │          │
             │  ┌───────▼───────┐  │
             │  │ Task Transmit │  │
             │  │ Core 0        │  │
             │  └───────┬───────┘  │
             └──────────┼──────────┘
                        │ Wi-Fi
                        ▼
               ┌─────────────────┐
               │   MQTT Broker   │
               └────────┬────────┘
                        │
                 ┌──────▼──────┐
                 │  Web / App  │
                 │ Dashboard   │
                 └─────────────┘

      DHT22 ───────► ESP32
      ESP32 ───────► Relay / Buzzer
```

## 3. Phần cứng sử dụng

| STT | Linh kiện | Chức năng |
|---|---|---|
| 1 | ESP32 DevKit V1 | Vi điều khiển trung tâm, kết nối Wi-Fi |
| 2 | MQ-2 | Phát hiện khí gas và khói |
| 3 | DHT22 | Đo nhiệt độ và độ ẩm |
| 4 | Relay Module 1 | Điều khiển quạt |
| 5 | Relay Module 2 | Điều khiển máy bơm |
| 6 | Buzzer | Phát âm thanh cảnh báo |

### Sơ đồ kết nối GPIO

| Thiết bị | Chân ESP32 |
|---|---|
| MQ-2 AOUT | GPIO 34 |
| DHT22 DATA | GPIO 15 |
| Relay quạt | GPIO 26 |
| Relay máy bơm | GPIO 27 |
| Buzzer | GPIO 25 |

## 4. Thiết kế phần mềm – FreeRTOS

Hệ thống được chia thành 3 Task độc lập nhằm đảm bảo khả năng xử lý song song và phân tách chức năng.

### Task 1: TaskRead – Đọc dữ liệu cảm biến

- Core: 1
- Đọc giá trị ADC từ cảm biến MQ-2.
- Đọc nhiệt độ và độ ẩm từ DHT22.
- Hiển thị dữ liệu qua Serial Monitor.
- Gửi dữ liệu vào `dataQueue` và `dataAlert`.
- Chu kỳ đọc: 2 giây.

### Task 2: TaskAlert – Xử lý cảnh báo

- Core: 1
- Nhận dữ liệu từ `dataAlert`.
- So sánh dữ liệu với các ngưỡng cảnh báo.
- Điều khiển relay quạt và máy bơm.
- Điều khiển buzzer khi phát hiện điều kiện bất thường.
- Nhận lệnh điều khiển thủ công từ Web thông qua `commandQueue`.

### Task 3: TaskTransmit – Truyền dữ liệu MQTT

- Core: 0
- Kết nối Wi-Fi.
- Kết nối MQTT Broker.
- Nhận dữ liệu từ `dataQueue`.
- Đóng gói dữ liệu cảm biến theo định dạng JSON.
- Publish dữ liệu lên MQTT Broker.
- Subscribe các topic điều khiển thiết bị.

### Cơ chế giao tiếp giữa các Task

Sử dụng FreeRTOS Queue để trao đổi dữ liệu giữa các tác vụ:

| Queue | Chức năng |
|---|---|
| `dataQueue` | Truyền dữ liệu cảm biến đến TaskTransmit |
| `dataAlert` | Truyền dữ liệu cảm biến đến TaskAlert |
| `commandQueue` | Nhận lệnh điều khiển từ MQTT Callback |

## 5. Nguyên lý hoạt động

### Chế độ tự động (AUTO)

Hệ thống liên tục đọc dữ liệu cảm biến và kiểm tra các ngưỡng:

| Thông số | Ngưỡng cảnh báo |
|---|---|
| Khí gas (ADC) | 2000 |
| Nhiệt độ | 50°C |

- Khi giá trị MQ-2 lớn hơn 2000: kích hoạt quạt và buzzer.
- Khi nhiệt độ lớn hơn 50°C: kích hoạt máy bơm và buzzer.
- Khi cả hai thông số vượt ngưỡng: kích hoạt đồng thời các thiết bị tương ứng.

### Chế độ điều khiển thủ công (MANUAL)

Người dùng có thể điều khiển quạt và máy bơm từ giao diện Web với các lựa chọn:

- `ON`: Bật thiết bị.
- `OFF`: Tắt thiết bị.
- `AUTO`: Chuyển về chế độ tự động.

## 6. Giao tiếp MQTT

Dự án sử dụng MQTT để truyền dữ liệu cảm biến và nhận lệnh điều khiển.

**MQTT Broker:** `broker.emqx.io`

**Port:** `1883`

### Các MQTT Topic

| Topic | Chức năng |
|---|---|
| `hoang/fire/sensor` | Truyền dữ liệu cảm biến |
| `hoang/fire/cmd/fan` | Nhận lệnh điều khiển quạt |
| `hoang/fire/cmd/pump` | Nhận lệnh điều khiển máy bơm |

### Định dạng dữ liệu JSON

ESP32 gửi dữ liệu cảm biến lên MQTT Broker theo định dạng:

```json
{
  "gas": 1850,
  "temp": 32.5,
  "hum": 70.2
}
```

Trong đó:
- `gas`: Giá trị ADC đọc từ cảm biến MQ-2.
- `temp`: Nhiệt độ môi trường (°C).
- `hum`: Độ ẩm môi trường (%).

## 7. Giao diện Web Dashboard

Giao diện Web cho phép người dùng giám sát và điều khiển hệ thống từ xa.

Các chức năng chính:

- Hiển thị nồng độ khí gas/khói.
- Hiển thị nhiệt độ môi trường.
- Biểu đồ theo dõi dữ liệu theo thời gian thực.
- Điều khiển quạt từ xa.
- Điều khiển máy bơm từ xa.
- Chuyển đổi giữa chế độ AUTO và MANUAL.

Web sử dụng MQTT over WebSocket để giao tiếp trực tiếp với MQTT Broker.

## 8. Công nghệ và công cụ sử dụng

| Công nghệ | Mục đích |
|---|---|
| ESP32 | Nền tảng vi điều khiển |
| Arduino Framework | Lập trình firmware |
| C/C++ | Ngôn ngữ lập trình |
| FreeRTOS | Quản lý đa nhiệm |
| MQTT | Giao thức truyền thông IoT |
| Wi-Fi | Kết nối mạng không dây |
| HTML/CSS/JavaScript | Xây dựng giao diện Web |
| MQTT.js | Kết nối MQTT trên Web |
| Chart.js | Biểu diễn dữ liệu bằng biểu đồ |
| Wokwi | Mô phỏng phần cứng và hệ thống |

## 9. Thư viện cần thiết

Cài đặt các thư viện sau trong Arduino IDE hoặc PlatformIO:

- `PubSubClient`
- `DHT sensor library`
- `Adafruit Unified Sensor`

Các thư viện Wi-Fi và FreeRTOS được tích hợp trong ESP32 Arduino Core.



## 10. Kết quả thực hiện

Hệ thống hướng đến các chức năng:

- Đọc và hiển thị dữ liệu cảm biến qua Serial Monitor.
- Xử lý đồng thời nhiều tác vụ bằng FreeRTOS.
- Truyền dữ liệu cảm biến lên MQTT Broker.
- Giám sát dữ liệu thông qua Web Dashboard.
- Cảnh báo bằng buzzer khi vượt ngưỡng.
- Tự động và thủ công điều khiển quạt, máy bơm.



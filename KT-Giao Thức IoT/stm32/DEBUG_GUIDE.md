# HƯỚNG DẪN DEBUG ĐIỀU KHIỂN STM32

## Vấn đề
ESP32 giao tiếp với Firebase OK, nhưng STM32 không điều khiển thiết bị.

## Các thay đổi đã thực hiện

### 1. Sửa UART callback (main.c)
- ✅ Nhận cả `\r` và `\n` thay vì chỉ `\n`
- ✅ Tránh xử lý buffer rỗng khi nhận `\r\n` kép
- ✅ Thêm hỗ trợ key "LD" cho LED parsing

### 2. Thêm Debug Output qua USB CDC
- ✅ STM32 sẽ echo lại tất cả lệnh nhận được qua USB
- ✅ Hiển thị các action thực thi (PUMP, LED, SERVO)

## Kiểm tra kết nối phần cứng

### Kết nối UART giữa ESP32 và STM32:
```
ESP32 GPIO17 (TX) → STM32 PA10 (USART1_RX)
ESP32 GPIO16 (RX) → STM32 PA9  (USART1_TX)
GND chung
```

### Kết nối thiết bị điều khiển:
```
STM32 PB0 → Relay/Pump
STM32 PB1 → LED
STM32 PA8 (TIM1_CH1) → Servo PWM
```

## Các bước debug

### Bước 1: Kiểm tra ESP32
1. Mở Serial Monitor ESP32 (115200 baud)
2. Toggle control trên web interface
3. Kiểm tra output:
   - `[STREAM] ===== TRIGGERED =====` - Stream từ Firebase hoạt động
   - `[STREAM] Sending to STM32: PUMP:1,LED:0,SERVO:50` - ESP32 gửi lệnh
   - `[->STM32] PUMP:1,LED:0,SERVO:50` - Lệnh đã gửi qua UART

**Nếu KHÔNG thấy các dòng trên:**
- Kiểm tra WiFi connection
- Kiểm tra Firebase config (FIREBASE_HOST, FIREBASE_AUTH)
- Kiểm tra path Firebase: `/agricontrol/irrigation/khuA`

### Bước 2: Kiểm tra STM32 nhận UART
1. Kết nối STM32 qua USB (cổng ST-Link hoặc USB User)
2. Mở Serial Monitor STM32 (COMx port)
3. Toggle control trên web
4. **Nếu STM32 nhận được lệnh**, bạn sẽ thấy:
   ```
   [UART RX] PUMP:1,LED:0,SERVO:50
   [ACTION] PUMP=1
   [ACTION] LED=0
   [ACTION] SERVO=50 (CCR=75)
   ```

**Nếu KHÔNG thấy output:**
- Kiểm tra lại kết nối UART (TX-RX, RX-TX)
- Kiểm tra GND chung
- Kiểm tra baudrate (phải là 115200)
- Dùng logic analyzer hoặc oscilloscope kiểm tra tín hiệu

### Bước 3: Kiểm tra thiết bị điều khiển
**Nếu STM32 nhận được lệnh nhưng thiết bị không hoạt động:**

1. **Kiểm tra Pump (PB0):**
   - Dùng multimeter đo điện áp PB0
   - Khi PUMP=1: phải có ~3.3V
   - Khi PUMP=0: phải có 0V
   - Kiểm tra relay có hoạt động không

2. **Kiểm tra LED (PB1):**
   - Dùng multimeter đo điện áp PB1
   - Khi LED=1: phải có ~3.3V
   - Khi LED=0: phải có 0V

3. **Kiểm tra Servo (PA8 - TIM1_CH1):**
   - Dùng oscilloscope kiểm tra PWM
   - Period: 20ms (50Hz)
   - Pulse width:
     - SERVO=0: 1ms (CCR=50)
     - SERVO=50: 1.5ms (CCR=75) - STOP
     - SERVO=100: 2ms (CCR=100)

## Test thủ công STM32

Để test trực tiếp STM32 mà không cần ESP32:

1. Kết nối USB-TTL adapter vào STM32:
   ```
   USB-TTL TX → STM32 PA10 (RX)
   USB-TTL RX → STM32 PA9 (TX)
   GND chung
   ```

2. Mở Serial Terminal (115200 baud)

3. Gửi lệnh test:
   ```
   PUMP:1,LED:1,SERVO:50
   ```

4. Quan sát debug output và thiết bị

## Các vấn đề thường gặp

### 1. STM32 không nhận UART
- **Nguyên nhân:** Sai kết nối TX-RX
- **Giải pháp:** Đảm bảo TX của ESP32 nối với RX của STM32

### 2. Nhận được lệnh nhưng không parse
- **Nguyên nhân:** Format lệnh sai
- **Giải pháp:** Kiểm tra format: `KEY:VALUE,KEY:VALUE`
- Các key hợp lệ: PUMP/P, LED/L/LD, SERVO/S

### 3. Parse OK nhưng thiết bị không hoạt động
- **Nguyên nhân:** 
  - GPIO không được cấu hình đúng
  - Thiết bị ngoại vi hỏng
  - Nguồn cấp không đủ
- **Giải pháp:** Kiểm tra phần cứng

### 4. Servo không quay
- **Nguyên nhân:**
  - PWM không hoạt động
  - Servo không được cấp nguồn
  - Tần số PWM sai
- **Giải pháp:**
  - Kiểm tra TIM1 đã start chưa
  - Kiểm tra nguồn 5V cho servo
  - Xác nhận period = 1000 (20ms @ 50kHz timer)

## Code quan trọng cần kiểm tra

### ESP32 (doan2.ino):
- Dòng 125-133: `sendCommandToSTM32()` - Format lệnh
- Dòng 143-252: `streamCallback()` - Xử lý Firebase stream
- Dòng 279-282: Serial2 config (RX=16, TX=17, baud=115200)

### STM32 (main.c):
- Dòng 194-217: `HAL_UART_RxCpltCallback()` - UART interrupt
- Dòng 224-290: `process_uart_command()` - Parse và điều khiển
- Dòng 305: `start_uart_rx_interrupt()` - Khởi động UART RX

### Web (index.html):
- Dòng 645-656: Event listener cho control change
- Dòng 612-659: `syncIrrigationControl()` - Sync với Firebase

## Liên hệ
Nếu vẫn gặp vấn đề, cung cấp:
1. Screenshot Serial Monitor ESP32
2. Screenshot Serial Monitor STM32 (nếu có output)
3. Ảnh chụp kết nối phần cứng
4. Kết quả đo điện áp GPIO

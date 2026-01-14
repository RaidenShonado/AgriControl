# AgriControl Dashboard (Web + Flutter Mobile)

## 1) Tổng quan dự án
**AgriControl** là hệ thống quản lý nông nghiệp (dashboard) theo thời gian thực, bao gồm:

- **Web Dashboard**: quản lý/giám sát theo khu vực (Khu A/B/C) với các tab chức năng (Vi khí hậu, Tưới tiêu, Cây trồng, Dự báo, Tài nguyên, Kinh tế…).
- **Mobile App (Flutter - Android)**: phiên bản ứng dụng điện thoại giúp người dùng theo dõi thông tin và thao tác điều khiển nhanh tương tự Web.
- **Firebase Realtime Database (RTDB)**: lưu trữ dữ liệu trạng thái, cấu hình, và đồng bộ realtime giữa Web và App.

Mục tiêu chính của dự án là:
- Đồng bộ dữ liệu **realtime** giữa các giao diện.
- Tạo trải nghiệm điều khiển/giám sát thuận tiện trên cả **trình duyệt** và **điện thoại Android**.
- Thiết kế giao diện trực quan, dễ sử dụng theo mô hình dashboard.

---

## 2) Kiến trúc hệ thống
### 2.1 Thành phần chính
- **Web UI** (HTML/CSS/JS): hiển thị dashboard và thao tác điều khiển.
- **Flutter App**: ứng dụng Android với giao diện tương tự, điều khiển và hiển thị dữ liệu.
- **Firebase RTDB**: cơ chế đồng bộ realtime dữ liệu và trạng thái.

### 2.2 Luồng đồng bộ dữ liệu (Realtime)
- Web/App **đọc dữ liệu** từ RTDB để hiển thị các giá trị cảm biến, trạng thái thiết bị, cấu hình.
- Web/App **ghi dữ liệu** lên RTDB khi người dùng thao tác:
  - Bật/tắt thiết bị
  - Thay đổi cài đặt, tham số (tuỳ tab)
- Firebase cập nhật realtime ⇒ mọi client khác đang mở sẽ thấy thay đổi ngay.

---

## 3) Tính năng (Web Dashboard)
### 3.1 Giao diện tổng quan
Web dashboard được tổ chức theo dạng sidebar + nội dung chính, các trang/tab chức năng được chia theo nhóm nghiệp vụ (climate/irrigation/crops/…).

### 3.2 Các chức năng chính
Tùy theo cấu hình dự án, Web thường bao gồm:

- **Vi khí hậu (Climate)**
  - Hiển thị nhiệt độ, độ ẩm, thông số môi trường theo khu vực (Khu A/B/C)
  - Cảnh báo vượt ngưỡng (nếu có cấu hình)
- **Tưới tiêu (Irrigation)**
  - Điều khiển bật/tắt hệ thống tưới theo khu
  - Thiết lập chế độ tưới (manual/auto) (nếu có)
- **Cây trồng (Crops)**
  - Quản lý trạng thái khu trồng
  - Hiển thị danh sách/chỉ số theo khu
- **Dự báo (Forecast)** (nếu có)
  - Hiển thị dự báo, thông tin thời tiết hoặc dữ liệu dự đoán
- **Tài nguyên (Resources)** (nếu có)
  - Theo dõi tài nguyên (nước, điện, vật tư…)
- **Kinh tế (Economics)** (nếu có)
  - Ước tính chi phí, thống kê theo thời gian
- **Thông báo/Notifications**
  - Cảnh báo và lịch sử sự kiện (nếu có triển khai)

---

## 4) Cấu trúc thư mục repo (gợi ý chuẩn để up GitHub)
Bạn có thể tổ chức repo theo cấu trúc:

```
AgriControl/
  web/
    index.html
    assets/
    css/
    js/
  mobile/
    pubspec.yaml
    android/
    lib/
      main.dart
      src/
        app/
        screens/
        services/
        widgets/
        utils/
  firebase/
    argicontrol-default-rtdb-export.json
  README.md
```

Ghi chú:
- Thư mục `firebase/` có thể chứa file export RTDB hoặc rules (nếu bạn quản lý rules riêng).
- `mobile/` là dự án Flutter hoàn chỉnh.
- `web/` là mã nguồn dashboard web.

---

## 5) Hướng dẫn chạy Web (Local)
### 5.1 Cách 1: mở trực tiếp HTML
- Vào thư mục `web/`
- Mở `index.html` bằng trình duyệt

Lưu ý: nếu web dùng module import hoặc gọi tài nguyên ngoài, bạn nên chạy bằng server local.

### 5.2 Cách 2: chạy bằng Live Server
- Dùng VSCode → cài extension **Live Server**
- Right click `web/index.html` → **Open with Live Server**

---

## 6) Hướng dẫn chạy Flutter (Android)
### 6.1 Yêu cầu môi trường
- Flutter SDK (khuyến nghị bản stable mới)
- Android Studio + Android SDK
- Thiết bị Android hoặc Android Emulator

Kiểm tra môi trường:
```bash
flutter doctor
```

### 6.2 Chạy app
Vào thư mục `mobile/`:
```bash
flutter pub get
flutter run
```

### 6.3 Firebase Options (Quan trọng)
Flutter thường cần file cấu hình `firebase_options.dart` được sinh ra từ FlutterFire CLI.

Nếu dự án đã có sẵn file này trong `mobile/lib/firebase_options.dart` thì chạy bình thường.

Nếu thiếu, bạn tạo lại bằng:
```bash
dart pub global activate flutterfire_cli
flutterfire configure
```

Sau đó chạy lại:
```bash
flutter pub get
flutter run
```

---

## 7) Đồng bộ Firebase RTDB
### 7.1 Mục tiêu đồng bộ
- Web và Flutter cùng đọc/ghi trên **cùng một cấu trúc path** RTDB
- Khi Web thay đổi trạng thái thiết bị ⇒ App cập nhật realtime
- Khi App thay đổi trạng thái ⇒ Web cập nhật realtime

### 7.2 Lưu ý khi triển khai Authentication
Tùy cấu hình dự án:
- Anonymous Sign-in (đăng nhập ẩn danh)
- Email/Password
- Hoặc chỉ đọc public (không khuyến nghị)

Nếu dùng Email/Password:
- Đảm bảo bật **Authentication → Email/Password** trên Firebase Console
- Đảm bảo rules RTDB cho phép user đã đăng nhập đọc/ghi theo thiết kế

---

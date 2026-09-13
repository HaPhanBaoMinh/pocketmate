# pocketmate

Thiết bị chỉ đường dùng ESP32-S3 có màn hình, nhận dữ liệu từ điện thoại.

Trạng thái ngày 13/09/2026: bo đã xác định là LCDWiki 2.8inch ESP32-S3 Display (N16R8, ILI9341V). Firmware bản thử ANCS đã chạy trên bo, hiển thị trạng thái và mã ghép nối lên LCD; đã nhận chỉ dẫn Google Maps thật từ iPhone 11 qua ANCS (không cần app) và vẽ mũi tên, khoảng cách, tên đường lên LCD.

- [Bản thử hiện tại: ANCS + màn hình, không cần app](docs/ancs-prototype.vi.md)
- [Kế hoạch dự phòng: iPhone chia sẻ màn hình Maps](docs/iphone-screen-mirroring-plan.vi.md)
- [Khảo sát phương án ban đầu](docs/implementation-plan.vi.md)
- [Kết quả kiểm tra phần cứng](docs/hardware-probe.md)

Hướng đang thử: iPhone 11 (iOS 18.1.1) ghép BLE với bo, không cài app; bo đọc thông báo Google Maps qua ANCS và hiện chỉ dẫn. Nếu Maps không gửi đủ nội dung qua thông báo, quay lại kế hoạch dự phòng chia sẻ màn hình (cần app iOS và môi trường build macOS).

# Bản thử ANCS + màn hình (iPhone, không cần app)

Cập nhật: 13/09/2026. Trạng thái: firmware chạy trên bo, chờ ghép nối iPhone thật.

## Mục tiêu bước này

ESP32-S3 nhận thông báo Google Maps từ iPhone qua Apple Notification Center Service (ANCS),
không cài app trên điện thoại, và hiển thị chỉ dẫn lên màn hình 2,8 inch. Bước này chưa
truyền hình ảnh bản đồ; nó chỉ chứng minh Maps có gửi nội dung chỉ đường qua thông báo hay không.

## Phần cứng đã xác nhận

- Bo: LCDWiki 2.8inch ESP32-S3 Display (ES3C28P cảm ứng / ES3N28P không cảm ứng), ESP32-S3 N16R8.
- Firmware gốc tên `2.8_ESP32S3_AP`, đã sao lưu 16 MB ở `~/.local/state/pocketmate/backups/`.
- LCD ILI9341V 240x320 SPI: CS=10, DC=46, SCK=12, MOSI=11, MISO=13, đèn nền=45, reset chung với EN.
- Cảm ứng FT6336G I2C: SDA=16, SCL=15, RST=18, INT=17 (chưa dùng).
- Cổng USB trên Linux: `/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_7C:E8:B1:B1:B9:E4-if00`.

## Cấu trúc firmware (`firmware/main`)

| File | Vai trò |
|---|---|
| `main.c` | BLE quảng bá, ghép nối MITM (passkey hiện trên LCD và USB), tìm ANCS, xếp hàng yêu cầu thuộc tính |
| `ancs_protocol.c` | Đóng/mở gói ANCS, chịu được gói bị chia nhỏ; có test trên máy |
| `gfx.c`, `fonts.c` | Vẽ chữ bitmap 1-bit có dấu tiếng Việt, cắt theo dải 40 dòng; không phụ thuộc ESP-IDF |
| `ui.c` | Bố cục màn hình: thanh trạng thái, mã ghép nối, chỉ dẫn Maps, banner "Chưa cập nhật" |
| `display.c` | SPI + chuỗi khởi tạo ILI9341V của hãng, đọc ID để tự kiểm tra dây |

Chỉ thông báo có app ID `com.google.Maps` mới được lấy tiêu đề/nội dung. Với app khác, firmware
chỉ đếm số lượng, không đọc nội dung.

## Lệnh thường dùng

```bash
tools/test-protocol.sh            # test bộ giải mã ANCS trên máy (ASan/UBSan)
tools/preview.sh maps stale passkey idle   # xem bố cục màn hình dưới dạng PNG trong /tmp
python3 tools/gen_font.py         # sinh lại fonts.c (cần Pillow + Noto Sans Bold)
tools/idf.sh build                # build ESP-IDF v5.5.1 (~/.local/share/pocketmate/esp-idf)
tools/idf.sh -p <cổng> flash      # nạp
~/.espressif/python_env/idf5.5_py3.12_env/bin/python tools/serial_monitor.py --port <cổng> --seconds 60
```

## Vì sao iPhone không thấy Pocketmate trong Cài đặt → Bluetooth (13/09/2026)

Bản đầu chỉ quảng bá "service solicitation" ANCS. Laptop Linux thấy và kết nối được (log USB ghi
`connected` → `paired` → không tìm thấy ANCS → ngắt 0x13, đúng dấu hiệu của máy tính, không phải
iPhone), nhưng iPhone 11 iOS 18.1.1 không liệt kê thiết bị. iOS chỉ hiện các thiết bị BLE nó nhận
dạng được; ví dụ `ble_ancs` chính thức của Espressif quảng bá thêm UUID dịch vụ HID (0x1812) và
appearance Generic HID để được liệt kê. Firmware nay làm giống vậy (không cài đặt dịch vụ HID thật).
Đồng thời đã xóa Pocketmate khỏi bộ nhớ Bluetooth của laptop để laptop không tự kết nối lại và
chiếm kết nối.

Khi thử với điện thoại, đừng bấm kết nối từ laptop: bo chỉ nhận một kết nối, laptop chiếm chỗ thì
iPhone không thấy. Log USB ghi địa chỉ và loại địa chỉ của thiết bị kết nối (public = máy tính,
random = điện thoại) và kiểu ghép nối (có passkey hay Just Works) để phân biệt.

## Quy trình thử với iPhone

1. Bo hiện "Chờ iPhone: Bluetooth > Pocketmate". Trên iPhone: Cài đặt → Bluetooth → chọn **Pocketmate**.
2. iPhone hỏi mã: mã 6 số hiện trên LCD (và dòng `PAIRING_CODE` trong USB log). Nhập mã.
3. iPhone hỏi **Chia sẻ thông báo hệ thống** (Share System Notifications): chọn Cho phép.
   Nếu từ chối, bo hiện "Bật Share System Notifications" và thử lại mỗi 5 giây.
4. Mở Google Maps, bắt đầu chỉ đường, rồi chuyển Maps xuống nền hoặc khóa máy.
5. Quan sát LCD và dòng `MAPS {...}` trong USB log. Đây là bằng chứng quyết định:
   nếu Maps không phát thông báo có nội dung rẽ/khoảng cách, hướng ANCS không đủ và phải quay lại
   phương án app + chia sẻ màn hình.

## Điều chưa làm

- Chưa có iPhone ghép nối thật, nên chưa biết Maps gửi những gì qua ANCS.
- Chưa vẽ mũi tên/khoảng cách riêng; hiện đang hiển thị nguyên văn tiêu đề và nội dung thông báo.
- Chưa dùng cảm ứng, loa, LED, thẻ nhớ.
- Font 1-bit chưa khử răng cưa; kích thước chữ có thể cần chỉnh sau khi thử ngoài trời.

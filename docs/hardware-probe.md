# Kiểm tra phần cứng — 13/09/2026

## Kết quả đọc trực tiếp

| Thuộc tính | Kết quả |
|---|---|
| USB VID:PID | `303a:1001` |
| USB product | Espressif USB JTAG/serial debug unit |
| Cổng tại thời điểm kiểm tra | `/dev/ttyACM1` |
| Chip | ESP32-S3 (QFN56), revision v0.2 |
| Tính năng do esptool báo | Wi-Fi, BT 5 (LE), Dual Core + LP Core, 240 MHz |
| PSRAM trong package | 8 MB, AP_3v3 |
| Flash nhận diện | 16 MB |
| Flash manufacturer/device ID | `5e` / `4018` |
| Flash eFuse configuration | Quad, 3.3 V |
| Crystal | 40 MHz |
| USB mode | USB-Serial/JTAG |

`/dev/ttyACM0` là bàn phím ZMK Sofle, không phải bo ESP32. Tên tty có thể thay đổi khi rút/cắm; khi thao tác nên chọn đường dẫn Espressif trong `/dev/serial/by-id/`.

## Cách kiểm tra

Đã dùng `lsusb`, `udevadm info` và esptool 5.4.0 trong môi trường Python riêng tại `/tmp/pocketmate-probe-venv`.

Lệnh nhận diện đã chạy thành công:

```sh
/tmp/pocketmate-probe-venv/bin/esptool --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_7C:E8:B1:B1:B9:E4-if00 --no-stub flash-id
```

Lệnh giao tiếp với ROM bootloader, đọc nhận dạng và reset bo về sau khi hoàn tất. Không chạy write-flash, erase-flash hoặc ghi eFuse. Firmware gốc chưa bị thay thế. Môi trường trong `/tmp` có thể bị dọn sau này.

## Chưa xác định

- Hãng/model bo, revision PCB và sơ đồ chân.
- Kích thước, độ phân giải, IC điều khiển và giao tiếp của màn hình.
- Touch controller, nút bấm, PMIC, pin và mạch sạc.
- Dung lượng PSRAM sử dụng ổn định khi chạy firmware: số 8 MB trên là nhận dạng chip, chưa phải kiểm thử RAM.
- Firmware/demo gốc, độ sáng ngoài trời và điện năng tiêu thụ.

USB và flash ID không đủ để suy ra model màn hình. Cần link sản phẩm hoặc mã in trên PCB trước khi chọn driver và pin mapping.

## Đối chiếu link người dùng cung cấp

Người dùng gửi [LCDWiki 2.8inch ESP32-32E Display](https://www.lcdwiki.com/2.8inch_ESP32-32E_Display). Trang này mô tả ESP32-D0WD-V3 / ESP32-32E, flash 4 MB, LCD 240 × 320 dùng ILI9341V qua SPI; bản cảm ứng dùng XPT2046. Đây là thông tin của sản phẩm trên trang, chưa phải cấu hình đã xác nhận của bo đang cắm.

Thông tin trên mâu thuẫn với chip đã đọc trực tiếp: ESP32-S3, flash 16 MB, PSRAM 8 MB. Quét USB lại sau khi nhận link vẫn thấy cùng thiết bị Espressif `303a:1001` và cùng serial tại `/dev/ttyACM1`; chưa thấy USB bridge CH340 trong danh sách USB hiện tại.

Chưa chọn pin mapping/driver từ trang ESP32-32E cho bo S3. Cần ảnh rõ mặt sau bo, chữ trên module và mã PCB để đối chiếu; có thể link thuộc biến thể khác hoặc thiết bị đang quét là bo khác. Không suy ra nguyên nhân khi chưa có bằng chứng. Không nạp firmware trong lượt đối chiếu này.

Người dùng bổ sung ảnh minh họa sản phẩm: chữ trên ảnh là `2.8" LCD Display`, `ESP32-32E 240x320`, `Resistance Touch`. Ảnh khớp dòng sản phẩm trong link, nhưng không có chữ nhận dạng chip trên vỏ module để đối chiếu với thiết bị USB. Bước tiếp theo: người dùng rút riêng cáp USB bo màn hình, kiểm tra thiết bị nào biến mất, sau đó cắm lại và đọc nhận dạng đúng cổng. Không tự gán flash/PSRAM của S3 đã quét cho sản phẩm ESP32-32E trong ảnh.

### Kết quả sau khi người dùng báo đã rút bo

Quét `lsusb` không còn thiết bị Espressif `303a:1001`; `/dev/serial/by-id/` chỉ còn bàn phím Sofle. Đường dẫn serial Espressif đã biến mất. Kết quả này liên hệ thiết bị vừa rút với thiết bị S3 đã đọc trước đó. Chờ cắm lại để đọc xác nhận; vẫn chưa biết model màn hình hoặc pin mapping của bo thực tế.

### Xác nhận sau khi cắm lại

Thiết bị `303a:1001` xuất hiện trở lại, cùng serial `7C:E8:B1:B1:B9:E4`, tại `/dev/ttyACM1`. Chạy lại esptool 5.4.0 với `--no-stub flash-id` thành công: ESP32-S3 QFN56 revision v0.2, embedded PSRAM 8 MB, flash 16 MB (manufacturer `5e`, device `4018`). Bo được reset về sau khi đọc, không ghi/xóa firmware.

Đã hoàn thành đối chiếu rút/cắm và đọc chip: bo người dùng thao tác là ESP32-S3. Trang/ảnh ESP32-32E không khớp chip thực tế nên chưa dùng làm cấu hình phần cứng. Việc còn thiếu là ảnh chụp bo thực tế hoặc tài liệu đúng biến thể S3 để xác định màn hình và sơ đồ chân.

### Đọc tài liệu liên kết trên LCDWiki

Đã mở thêm [schematic](https://www.lcdwiki.com/res/E32R28T/2.8inch_ESP32-32_Display_Schematic.pdf), [user manual](https://www.lcdwiki.com/res/E32R28T/2.8inch_ESP32-32E_E32R28T_E32N28T_User_Manual.pdf) và [hướng dẫn demo ESP-IDF](https://www.lcdwiki.com/res/E32R28T/2.8inch_E32R28T_E32N28T_ESP-IDF_Demo_Instructions.pdf). Schematic ghi U2 là ESP32-WROOM-32E, U3 là CH340C. Như vậy khác biệt với chip S3 đã đo có cả trong schematic, không chỉ tiêu đề trang.

Pin LCD theo tài liệu của biến thể 32E: CS=15, DC=2, SCK=14, MOSI=13, MISO=12, BL=21, RESET chung EN. Đây là cấu hình tham khảo từ nhà sản xuất, chưa xác minh trên bo S3 thực tế. Chưa ghi các chân này vào firmware hoặc nạp demo 32E lên bo.

### Xác định đúng bo: LCDWiki 2.8inch ESP32-S3 Display

Bản sao lưu flash 16 MB (`~/.local/state/pocketmate/backups/factory-7ce8b1b1b9e4-20260913.bin`, SHA-256 đã lưu cạnh file) cho thấy app gốc tên `2.8_ESP32S3_AP`, build bằng Arduino và LVGL v8. Từ tên đó tìm được trang [2.8inch ESP32-S3 Display](https://www.lcdwiki.com/2.8inch_ESP32-S3_Display) (SKU ES3C28P cảm ứng điện dung / ES3N28P không cảm ứng), khớp chip ESP32-S3 N16R8 đã đo. Link ESP32-32E do người dùng gửi là biến thể khác của cùng dòng sản phẩm.

Sơ đồ chân theo [hướng dẫn demo ESP-IDF của hãng](https://www.lcdwiki.com/res/ES3C28P/2.8inch_ES3C28P_ES3N28P_ESP-IDF_Demo_Instructions.pdf): LCD ILI9341V SPI với CS=10, DC=46, SCK=12, MOSI=11, MISO=13, BL=45 (mức cao bật), RST chung EN. Cảm ứng FT6336G I2C: SDA=16, SCL=15, RST=18, INT=17. LED RGB WS2812 ở IO42, pin ADC IO9, thẻ SD SDIO 38/40/39/41/48/47, âm thanh I2S 1/4/5/8/7/6. Chuỗi khởi tạo LCD lấy từ `ILI9341V_Init.txt` của hãng (có lệnh 0x21 đảo màu cho tấm IPS, MADCTL 0x08).

Xác minh trên bo: firmware Pocketmate gửi chuỗi khởi tạo ở 5 MHz rồi đọc thanh ghi RDDPM (0x0A) nhận `0x9C`, đúng giá trị ILI9341 báo khi đã thoát sleep và bật hiển thị. Như vậy CS/DC/SCK/MOSI/MISO đúng. Lệnh RDDID (0xD3) trả 0xFF vì cần thêm xung dummy mà `esp_lcd` không phát; không dùng làm tiêu chí. Việc hình có hiện đúng màu/chiều hay không còn cần người dùng nhìn màn hình xác nhận.

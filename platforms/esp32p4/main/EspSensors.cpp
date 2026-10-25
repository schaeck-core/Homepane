#include "EspSensors.h"

#include "bsp/esp32_p4_wifi6_touch_lcd_4b.h"
#include "driver/i2c_master.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace platform_esp32p4 {

namespace {

constexpr const char* TAG = "sensors";
// Generous: the driver keeps a transfer's commands on the caller's stack,
// so a call must not return on its timeout while the transfer still runs.
// A normal transfer takes far below a millisecond: touch and audio do not
// wait longer.
constexpr int kI2cTimeoutMs = 200;

// I2C addresses (7-bit, docs/hardware/SENSORS.md). Taken by the board:
// GT911 0x14/0x5D, ES8311 0x18, ES7210 0x40.
constexpr uint16_t kShtc3 = 0x70;
constexpr uint16_t kBos1921 = 0x44; // assumed; confirm in the datasheet
constexpr uint16_t kEns160[] = {0x52, 0x53};
constexpr uint16_t kSgp40 = 0x59;

// SHT4x on a bus of its own while testing: pins that are easy to reach,
// they move later. The breakout sets 0x44 or 0x45.
constexpr int kSht4xScl = 46, kSht4xSda = 47;
constexpr uint16_t kSht4x[] = {0x44, 0x45};

// LD2410: 256000 baud, reports on its own about every 100 ms.
constexpr uart_port_t kRadarPort = UART_NUM_1;
constexpr int kRadarTx = 2, kRadarRx = 3;
constexpr int kRadarBaud = 256000;
constexpr int kRadarProbeMs = 600;

constexpr uint32_t kClimateIntervalMs = 5000;

// Sensirion CRC-8 (SHTC3, SHT4x, SGP40): polynomial 0x31, init 0xFF.
uint8_t sensirion_crc(const uint8_t* data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) crc = (crc & 0x80) ? uint8_t((crc << 1) ^ 0x31) : uint8_t(crc << 1);
    }
    return crc;
}

uint64_t now_ms() { return static_cast<uint64_t>(esp_timer_get_time() / 1000); }

i2c_master_dev_handle_t add_device(i2c_master_bus_handle_t bus, uint16_t addr, uint32_t speed_hz = 400000)
{
    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = addr;
    cfg.scl_speed_hz = speed_hz;
    i2c_master_dev_handle_t dev = nullptr;
    if (i2c_master_bus_add_device(bus, &cfg, &dev) != ESP_OK) return nullptr;
    return dev;
}

bool command(i2c_master_dev_handle_t dev, uint16_t cmd)
{
    const uint8_t buf[2] = {uint8_t(cmd >> 8), uint8_t(cmd & 0xFF)};
    return i2c_master_transmit(dev, buf, sizeof(buf), kI2cTimeoutMs) == ESP_OK;
}

// SHTC3: ID register (0xEFC8), bits 0x083F = 0x0807.
bool shtc3_identify(i2c_master_dev_handle_t dev)
{
    command(dev, 0x3517); // wake up
    vTaskDelay(pdMS_TO_TICKS(1));
    uint8_t id[3] = {};
    const bool ok = command(dev, 0xEFC8) && i2c_master_receive(dev, id, sizeof(id), kI2cTimeoutMs) == ESP_OK &&
                    sensirion_crc(id, 2) == id[2] && ((id[0] << 8 | id[1]) & 0x083F) == 0x0807;
    command(dev, 0xB098); // sleep
    return ok;
}

// One measurement, normal mode, temperature first, without clock stretching
// (0x7866): the bus is shared, nobody may hold SCL.
bool shtc3_measure(i2c_master_dev_handle_t dev, float& temp_c, float& rh_pct)
{
    command(dev, 0x3517);
    vTaskDelay(pdMS_TO_TICKS(1));
    bool ok = command(dev, 0x7866);
    uint8_t d[6] = {};
    if (ok) {
        vTaskDelay(pdMS_TO_TICKS(13)); // max. 12.1 ms
        ok = i2c_master_receive(dev, d, sizeof(d), kI2cTimeoutMs) == ESP_OK && sensirion_crc(d, 2) == d[2] &&
             sensirion_crc(d + 3, 2) == d[5];
    }
    command(dev, 0xB098);
    if (!ok) return false;
    temp_c = -45.0f + 175.0f * float(d[0] << 8 | d[1]) / 65536.0f;
    rh_pct = 100.0f * float(d[3] << 8 | d[4]) / 65536.0f;
    return true;
}

// SHT4x: serial number (0x89), two words with CRC. One-byte commands.
bool sht4x_identify(i2c_master_dev_handle_t dev, uint32_t& serial)
{
    const uint8_t cmd = 0x89;
    uint8_t d[6] = {};
    if (i2c_master_transmit(dev, &cmd, 1, kI2cTimeoutMs) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(2));
    if (i2c_master_receive(dev, d, sizeof(d), kI2cTimeoutMs) != ESP_OK || sensirion_crc(d, 2) != d[2] ||
        sensirion_crc(d + 3, 2) != d[5])
        return false;
    serial = uint32_t(d[0]) << 24 | uint32_t(d[1]) << 16 | uint32_t(d[3]) << 8 | d[4];
    return true;
}

// One measurement, high repeatability (0xFD). The SHT4x never stretches
// the clock.
bool sht4x_measure(i2c_master_dev_handle_t dev, float& temp_c, float& rh_pct)
{
    const uint8_t cmd = 0xFD;
    uint8_t d[6] = {};
    if (i2c_master_transmit(dev, &cmd, 1, kI2cTimeoutMs) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(10)); // max. 8.3 ms
    if (i2c_master_receive(dev, d, sizeof(d), kI2cTimeoutMs) != ESP_OK || sensirion_crc(d, 2) != d[2] ||
        sensirion_crc(d + 3, 2) != d[5])
        return false;
    temp_c = -45.0f + 175.0f * float(d[0] << 8 | d[1]) / 65535.0f;
    rh_pct = std::clamp(-6.0f + 125.0f * float(d[3] << 8 | d[4]) / 65535.0f, 0.0f, 100.0f);
    return true;
}

// ENS160: PART_ID register 0x00 reads 0x0160 (little-endian).
bool ens160_identify(i2c_master_dev_handle_t dev)
{
    const uint8_t reg = 0x00;
    uint8_t id[2] = {};
    return i2c_master_transmit_receive(dev, &reg, 1, id, sizeof(id), kI2cTimeoutMs) == ESP_OK && id[0] == 0x60 &&
           id[1] == 0x01;
}

// SGP40: serial number (0x3682), three words with CRC.
bool sgp40_identify(i2c_master_dev_handle_t dev)
{
    uint8_t d[9] = {};
    if (!command(dev, 0x3682)) return false;
    vTaskDelay(pdMS_TO_TICKS(1));
    if (i2c_master_receive(dev, d, sizeof(d), kI2cTimeoutMs) != ESP_OK) return false;
    for (int i = 0; i < 9; i += 3) {
        if (sensirion_crc(d + i, 2) != d[i + 2]) return false;
    }
    return true;
}

// LD2410 report frames: F4 F3 F2 F1, length (2, LE), type, 0xAA, target
// state, moving distance (2), moving energy, still distance (2), still
// energy, detection distance (2, cm), ..., 0x55, 0x00, F8 F7 F6 F5.
class Ld2410Parser {
public:
    // Feeds bytes; true when a complete report was parsed.
    bool feed(const uint8_t* data, size_t len)
    {
        bool got = false;
        for (size_t i = 0; i < len; ++i) got |= feed(data[i]);
        return got;
    }
    bool present() const { return state_ != 0; }
    float distance_m() const { return distance_cm_ / 100.0f; }

private:
    bool feed(uint8_t b)
    {
        static constexpr uint8_t kHead[4] = {0xF4, 0xF3, 0xF2, 0xF1};
        static constexpr uint8_t kTail[4] = {0xF8, 0xF7, 0xF6, 0xF5};
        if (pos_ < 4) {
            pos_ = b == kHead[pos_] ? pos_ + 1 : (b == kHead[0] ? 1 : 0);
            return false;
        }
        if (pos_ >= sizeof(buf_)) {
            pos_ = 0;
            return false;
        }
        buf_[pos_++] = b;
        if (pos_ < 6) return false;
        const size_t len = buf_[4] | buf_[5] << 8;
        if (len < 13 || 6 + len + 4 > sizeof(buf_)) {
            pos_ = 0;
            return false;
        }
        if (pos_ < 6 + len + 4) return false;
        pos_ = 0;
        if (std::memcmp(buf_ + 6 + len, kTail, 4) != 0 || buf_[7] != 0xAA) return false;
        state_ = buf_[8];
        distance_cm_ = buf_[15] | buf_[16] << 8;
        return true;
    }

    uint8_t buf_[64] = {};
    size_t pos_ = 0;
    uint8_t state_ = 0;
    uint16_t distance_cm_ = 0;
};

void add_part(std::string& parts, const char* name)
{
    if (!parts.empty()) parts += ", ";
    parts += name;
}

} // namespace

void EspSensors::start()
{
    if (started_) return;
    started_ = true;
    // Stack in PSRAM: internal RAM is the scarce one, and the task never
    // touches the flash.
    if (xTaskCreatePinnedToCoreWithCaps(task, "sensors", 4096, this, 2, nullptr, tskNO_AFFINITY, MALLOC_CAP_SPIRAM) !=
        pdPASS) {
        ESP_LOGW(TAG, "no task: no extra hardware");
        std::lock_guard<std::mutex> lock(mutex_);
        caps_.detected = true;
    }
}

app::sensors::SensorReadings EspSensors::read(uint64_t)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return readings_;
}

app::hardware::Capabilities EspSensors::capabilities() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return caps_;
}

void EspSensors::task(void* self)
{
    static_cast<EspSensors*>(self)->run();
    // a transfer that timed out may still be running on this stack: let it end
    vTaskDelay(pdMS_TO_TICKS(500));
    vTaskDeleteWithCaps(nullptr);
}

void EspSensors::run()
{
    const int64_t t0 = esp_timer_get_time();
    app::hardware::Capabilities caps;

    // ── I2C ──
    // the one temperature/humidity sensor that is read: SHTC3 or SHT4x
    i2c_master_dev_handle_t climate = nullptr;
    bool (*climate_measure)(i2c_master_dev_handle_t, float&, float&) = nullptr;
    const char* climate_part = nullptr;
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (!bus) {
        ESP_LOGW(TAG, "no I2C bus: no I2C sensors");
    } else {
        // Each part as a normal device, asked with its own ID command; a
        // part that is not there does not acknowledge. Not i2c_master_probe():
        // it switches the bus's interrupts and clock outside the driver's
        // normal path, and a late interrupt then read the probe's freed stack
        // (panics at boot, in whatever task ran: sensors, taskLVGL). The
        // absent parts' NACKs are expected: the driver's error log stays
        // quiet meanwhile.
        const esp_log_level_t i2c_log = esp_log_level_get("i2c.master");
        esp_log_level_set("i2c.master", ESP_LOG_NONE);

        if (i2c_master_dev_handle_t dev = add_device(bus, kShtc3)) {
            if (shtc3_identify(dev)) {
                climate = dev;
                climate_measure = shtc3_measure;
                climate_part = "SHTC3";
            } else {
                i2c_master_bus_rm_device(dev);
            }
        }
        if (i2c_master_dev_handle_t dev = add_device(bus, kBos1921)) {
            // its ID register is not confirmed yet: any answer counts; no driver
            const uint8_t reg = 0x00;
            uint8_t value = 0;
            if (i2c_master_transmit_receive(dev, &reg, 1, &value, 1, kI2cTimeoutMs) == ESP_OK) {
                ESP_LOGI(TAG, "BOS1921 (0x%02x) found, no driver yet: haptics stay off", kBos1921);
                add_part(caps.parts, "BOS1921");
            }
            i2c_master_bus_rm_device(dev);
        }
        for (uint16_t addr : kEns160) {
            i2c_master_dev_handle_t dev = add_device(bus, addr);
            if (!dev) continue;
            const bool found = ens160_identify(dev);
            i2c_master_bus_rm_device(dev);
            if (found) {
                ESP_LOGI(TAG, "ENS160 (0x%02x) found, no driver yet", addr);
                add_part(caps.parts, "ENS160");
                break;
            }
        }
        if (i2c_master_dev_handle_t dev = add_device(bus, kSgp40)) {
            if (sgp40_identify(dev)) {
                ESP_LOGI(TAG, "SGP40 found, no driver yet");
                add_part(caps.parts, "SGP40");
            }
            i2c_master_bus_rm_device(dev);
        }
        esp_log_level_set("i2c.master", i2c_log);
    }

    // ── SHT4x on its test bus ──
    i2c_master_bus_handle_t sht4x_bus = nullptr;
    if (!climate) {
        i2c_master_bus_config_t bc = {};
        bc.i2c_port = -1; // any free one (the BSP has I2C_NUM_1)
        bc.sda_io_num = gpio_num_t(kSht4xSda);
        bc.scl_io_num = gpio_num_t(kSht4xScl);
        bc.clk_source = I2C_CLK_SRC_DEFAULT;
        bc.glitch_ignore_cnt = 7;
        bc.flags.enable_internal_pullup = true; // weak: the breakout brings its own
        if (i2c_new_master_bus(&bc, &sht4x_bus) == ESP_OK) {
            const esp_log_level_t i2c_log = esp_log_level_get("i2c.master");
            esp_log_level_set("i2c.master", ESP_LOG_NONE);
            for (uint16_t addr : kSht4x) {
                i2c_master_dev_handle_t dev = add_device(sht4x_bus, addr, 100000);
                if (!dev) continue;
                uint32_t serial = 0;
                if (sht4x_identify(dev, serial)) {
                    ESP_LOGI(TAG, "SHT4x at 0x%02x (SCL %d, SDA %d), serial %08lx", addr, kSht4xScl, kSht4xSda,
                             (unsigned long)serial);
                    climate = dev;
                    climate_measure = sht4x_measure;
                    climate_part = "SHT4x";
                    break;
                }
                i2c_master_bus_rm_device(dev);
            }
            esp_log_level_set("i2c.master", i2c_log);
            if (!climate) {
                ESP_LOGI(TAG, "no SHT4x on SCL %d / SDA %d", kSht4xScl, kSht4xSda);
                i2c_del_master_bus(sht4x_bus);
                sht4x_bus = nullptr;
            }
        }
    }
    if (climate) {
        caps.temperature = caps.humidity = true;
        add_part(caps.parts, climate_part);
    }

    // ── mmWave (UART) ──
    bool radar = false;
    Ld2410Parser ld2410;
    uart_config_t uc = {};
    uc.baud_rate = kRadarBaud;
    uc.data_bits = UART_DATA_8_BITS;
    uc.parity = UART_PARITY_DISABLE;
    uc.stop_bits = UART_STOP_BITS_1;
    uc.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uc.source_clk = UART_SCLK_DEFAULT;
    if (uart_driver_install(kRadarPort, 1024, 0, 0, nullptr, 0) == ESP_OK &&
        uart_param_config(kRadarPort, &uc) == ESP_OK &&
        uart_set_pin(kRadarPort, kRadarTx, kRadarRx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) == ESP_OK) {
        uint8_t buf[64];
        const uint64_t until = now_ms() + kRadarProbeMs;
        while (!radar && now_ms() < until) {
            const int n = uart_read_bytes(kRadarPort, buf, sizeof(buf), pdMS_TO_TICKS(50));
            if (n > 0 && ld2410.feed(buf, n)) radar = true;
        }
        if (radar) {
            caps.presence = true; // LD2410: no target count, no positions
            add_part(caps.parts, "LD2410");
        } else {
            uart_driver_delete(kRadarPort); // nothing there (the LD6004 follows later)
        }
    }

    caps.detected = true;
    ESP_LOGI(TAG, "probed in %d ms: %s", int((esp_timer_get_time() - t0) / 1000),
             caps.parts.empty() ? "no extra hardware" : caps.parts.c_str());
    {
        std::lock_guard<std::mutex> lock(mutex_);
        caps_ = caps;
    }
    if (!climate && !radar) return;

    // ── keep reading what was found ──
    uint64_t next_climate = 0;
    int climate_errors = 0;
    uint8_t buf[64];
    for (;;) {
        if (radar) {
            const int n = uart_read_bytes(kRadarPort, buf, sizeof(buf), pdMS_TO_TICKS(100));
            if (n > 0 && ld2410.feed(buf, n)) {
                std::lock_guard<std::mutex> lock(mutex_);
                readings_.presence = ld2410.present();
                if (ld2410.present()) readings_.distance_m = ld2410.distance_m();
                else readings_.distance_m.reset();
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        if (climate && now_ms() >= next_climate) {
            next_climate = now_ms() + kClimateIntervalMs;
            float t = 0, rh = 0;
            if (climate_measure(climate, t, rh)) {
                climate_errors = 0;
                std::lock_guard<std::mutex> lock(mutex_);
                readings_.temperature_c = t;
                readings_.humidity_pct = rh;
            } else if (++climate_errors == 5) {
                // keeps failing: stop asking until the next boot (the bus is shared)
                ESP_LOGW(TAG, "%s stopped answering, no climate readings until restart", climate_part);
                i2c_master_bus_rm_device(climate);
                climate = nullptr;
                if (sht4x_bus) i2c_del_master_bus(sht4x_bus);
                std::lock_guard<std::mutex> lock(mutex_);
                readings_.temperature_c.reset();
                readings_.humidity_pct.reset();
            }
        }
        if (!climate && !radar) return;
    }
}

} // namespace platform_esp32p4

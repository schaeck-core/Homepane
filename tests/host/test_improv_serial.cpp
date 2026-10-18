// Unit tests for the Improv Wi-Fi serial packet format (web flasher sends
// the Wi-Fi credentials after flashing). Packets are built the way the
// browser side (improv-wifi-serial-sdk) builds and parses them.

#include "app/cpp/net/ImprovSerial.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace app::net::improv;

namespace {

int g_failures = 0;

void expect(bool condition, const std::string& description)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description.c_str());
        ++g_failures;
    } else {
        std::fprintf(stdout, "OK:   %s\n", description.c_str());
    }
}

// What the browser writes: header, version, type 3, length, data, checksum, newline.
std::vector<uint8_t> browser_rpc(uint8_t command, const std::vector<uint8_t>& payload)
{
    std::vector<uint8_t> data = {command, static_cast<uint8_t>(payload.size())};
    data.insert(data.end(), payload.begin(), payload.end());
    std::vector<uint8_t> p = {'I', 'M', 'P', 'R', 'O', 'V', 1, 3, static_cast<uint8_t>(data.size())};
    p.insert(p.end(), data.begin(), data.end());
    uint8_t sum = 0;
    for (uint8_t b : p) sum = static_cast<uint8_t>(sum + b);
    p.push_back(sum);
    p.push_back('\n');
    return p;
}

std::vector<uint8_t> wifi_payload(const std::string& ssid, const std::string& password)
{
    std::vector<uint8_t> p = {static_cast<uint8_t>(ssid.size())};
    p.insert(p.end(), ssid.begin(), ssid.end());
    p.push_back(static_cast<uint8_t>(password.size()));
    p.insert(p.end(), password.begin(), password.end());
    return p;
}

int feed_all(PacketReader& reader, const std::vector<uint8_t>& bytes, PacketReader::Result& last)
{
    int rpcs = 0;
    last = PacketReader::Result::None;
    for (uint8_t b : bytes) {
        PacketReader::Result r = reader.feed(b);
        if (r != PacketReader::Result::None) last = r;
        if (r == PacketReader::Result::Rpc) ++rpcs;
    }
    return rpcs;
}

// Parses a device packet the way the browser does: it must start a line.
bool browser_parse(const std::vector<uint8_t>& bytes, uint8_t& type, std::vector<uint8_t>& data)
{
    if (bytes.size() < 12 || bytes.front() != '\n' || bytes.back() != '\n') return false;
    std::vector<uint8_t> line(bytes.begin() + 1, bytes.end() - 1);
    if (std::string(line.begin(), line.begin() + 6) != "IMPROV" || line[6] != 1) return false;
    const size_t length = line[8];
    if (line.size() != 9 + length + 1) return false;
    uint8_t sum = 0;
    for (size_t i = 0; i + 1 < line.size(); ++i) sum = static_cast<uint8_t>(sum + line[i]);
    if (sum != line.back()) return false;
    type = line[7];
    data.assign(line.begin() + 9, line.begin() + 9 + length);
    return true;
}

void test_reads_rpcs()
{
    PacketReader reader;
    PacketReader::Result last;
    expect(feed_all(reader, browser_rpc(2, {}), last) == 1 && reader.rpc().command == 2 && reader.rpc().data.empty(),
           "a current-state request is read");

    std::vector<uint8_t> stream = {'h', 'e', 'l', 'l', 'o', '\n', 'I', 'M', 'P'};
    auto wifi = browser_rpc(1, wifi_payload("Home", "secret pw"));
    stream.insert(stream.end(), wifi.begin(), wifi.end());
    expect(feed_all(reader, stream, last) == 1 && reader.rpc().command == 1,
           "junk and a broken header before a packet are skipped");
    std::string ssid, password;
    expect(parse_wifi_settings(reader.rpc().data, ssid, password) && ssid == "Home" && password == "secret pw",
           "SSID and password are parsed");

    auto bad = browser_rpc(3, {});
    bad[bad.size() - 2] ^= 0xFF;
    feed_all(reader, bad, last);
    expect(last == PacketReader::Result::Invalid, "a wrong checksum is reported as invalid");
    expect(feed_all(reader, browser_rpc(3, {}), last) == 1 && reader.rpc().command == 3,
           "the reader recovers after a bad packet");
}

void test_wifi_settings_validation()
{
    std::string ssid, password;
    expect(parse_wifi_settings(wifi_payload("Open", ""), ssid, password) && ssid == "Open" && password.empty(),
           "an open network has an empty password");
    expect(!parse_wifi_settings({}, ssid, password), "empty data is rejected");
    expect(!parse_wifi_settings({0, 0}, ssid, password), "an empty SSID is rejected");
    expect(!parse_wifi_settings({4, 'H', 'o', 'm', 'e', 9, 'x'}, ssid, password),
           "a password longer than the data is rejected");
}

void test_device_packets()
{
    uint8_t type = 0;
    std::vector<uint8_t> data;
    expect(browser_parse(encode_state(State::Provisioned), type, data) && type == 1 && data == std::vector<uint8_t>{4},
           "a state packet parses in the browser");
    expect(browser_parse(encode_error(Error::UnableToConnect), type, data) && type == 2 &&
               data == std::vector<uint8_t>{3},
           "an error packet parses in the browser");

    expect(browser_parse(encode_result(Command::DeviceInfo, {"Homepane", "0.2.0", "ESP32-P4", "Homepane"}), type,
                         data) &&
               type == 4 && data[0] == 3 && data[1] == data.size() - 2 && data[2] == 8 &&
               std::string(data.begin() + 3, data.begin() + 11) == "Homepane",
           "an RPC result carries its strings with lengths");
    expect(browser_parse(encode_result(Command::ScanNetworks, {}), type, data) && data.size() == 2 && data[1] == 0,
           "an empty result ends a scan");

    std::vector<std::string> many(10, std::string(40, 'x'));
    expect(browser_parse(encode_result(Command::ScanNetworks, many), type, data) && data.size() <= 255,
           "long results are cut to fit one packet");
}

} // namespace

int main()
{
    test_reads_rpcs();
    test_wifi_settings_validation();
    test_device_packets();

    if (g_failures == 0) {
        std::printf("All Improv tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}

#ifndef APP_CPP_NET_IMPROVSERIAL_H
#define APP_CPP_NET_IMPROVSERIAL_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Improv Wi-Fi over serial (https://www.improv-wifi.com/serial/): lets the
// web flasher (ESP Web Tools) send the Wi-Fi credentials over the same USB
// cable right after flashing. This is only the packet format; the platform
// reads and writes the serial port and connects to the network.
//
// Packet: "IMPROV" <version 1> <type> <length> <data...> <checksum>, the
// checksum being the low byte of the sum of all bytes before it.
namespace app::net::improv {

enum class State : uint8_t {
    Ready = 0x02,        // waiting for credentials
    Provisioning = 0x03, // connecting
    Provisioned = 0x04,  // connected
};

enum class Error : uint8_t {
    None = 0x00,
    InvalidRpc = 0x01,
    UnknownCommand = 0x02,
    UnableToConnect = 0x03,
    Unknown = 0xFF,
};

enum class Command : uint8_t {
    WifiSettings = 0x01,
    CurrentState = 0x02,
    DeviceInfo = 0x03,
    ScanNetworks = 0x04,
};

struct Rpc {
    uint8_t command = 0;
    std::vector<uint8_t> data; // the command's payload
};

// Collects RPC packets from the incoming byte stream. Anything that is not
// an Improv packet (stray newlines, a terminal's input) is skipped.
class PacketReader {
public:
    enum class Result {
        None,    // need more bytes
        Rpc,     // rpc() holds a complete command
        Invalid, // a packet with a bad checksum or a malformed RPC
    };

    Result feed(uint8_t byte);
    const Rpc& rpc() const { return rpc_; }

private:
    std::vector<uint8_t> buf_;
    Rpc rpc_;
};

// SSID and password of a WifiSettings command. False if malformed.
bool parse_wifi_settings(const std::vector<uint8_t>& data, std::string& ssid, std::string& password);

// Device -> browser packets. Each is framed by newlines: the browser only
// looks for a packet at the start of a line, and log output may have left a
// line unfinished.
std::vector<uint8_t> encode_state(State state);
std::vector<uint8_t> encode_error(Error error);
// An RPC result: a list of strings (a scan sends one result per network and
// an empty one at the end). Strings are cut to fit the 255-byte packet.
std::vector<uint8_t> encode_result(Command command, const std::vector<std::string>& strings);

} // namespace app::net::improv

#endif // APP_CPP_NET_IMPROVSERIAL_H

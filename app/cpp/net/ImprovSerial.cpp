#include "ImprovSerial.h"

namespace app::net::improv {

namespace {

constexpr uint8_t kHeader[] = {'I', 'M', 'P', 'R', 'O', 'V'};
constexpr size_t kHeaderSize = sizeof(kHeader);
constexpr uint8_t kVersion = 1;
constexpr size_t kPrefixSize = kHeaderSize + 3; // header, version, type, length

enum class Type : uint8_t {
    CurrentState = 0x01,
    ErrorState = 0x02,
    Rpc = 0x03,
    RpcResult = 0x04,
};

std::vector<uint8_t> encode(Type type, const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> out;
    out.reserve(kPrefixSize + data.size() + 3);
    out.push_back('\n');
    out.insert(out.end(), kHeader, kHeader + kHeaderSize);
    out.push_back(kVersion);
    out.push_back(static_cast<uint8_t>(type));
    out.push_back(static_cast<uint8_t>(data.size()));
    out.insert(out.end(), data.begin(), data.end());
    uint8_t sum = 0;
    for (size_t i = 1; i < out.size(); ++i) sum = static_cast<uint8_t>(sum + out[i]);
    out.push_back(sum);
    out.push_back('\n');
    return out;
}

} // namespace

PacketReader::Result PacketReader::feed(uint8_t byte)
{
    if (buf_.size() < kHeaderSize) {
        if (byte == kHeader[buf_.size()]) {
            buf_.push_back(byte);
        } else {
            buf_.clear();
            if (byte == kHeader[0]) buf_.push_back(byte);
        }
        return Result::None;
    }

    buf_.push_back(byte);
    if (buf_.size() < kPrefixSize) return Result::None;
    const size_t length = buf_[kHeaderSize + 2];
    if (buf_.size() < kPrefixSize + length + 1) return Result::None;

    std::vector<uint8_t> packet;
    packet.swap(buf_);
    uint8_t sum = 0;
    for (size_t i = 0; i + 1 < packet.size(); ++i) sum = static_cast<uint8_t>(sum + packet[i]);
    if (sum != packet.back() || packet[kHeaderSize] != kVersion) return Result::Invalid;
    if (packet[kHeaderSize + 1] != static_cast<uint8_t>(Type::Rpc)) return Result::None; // not for us

    // RPC data: <command> <payload length> <payload...>
    const uint8_t* data = packet.data() + kPrefixSize;
    if (length < 2 || data[1] + 2u > length) return Result::Invalid;
    rpc_.command = data[0];
    rpc_.data.assign(data + 2, data + 2 + data[1]);
    return Result::Rpc;
}

bool parse_wifi_settings(const std::vector<uint8_t>& data, std::string& ssid, std::string& password)
{
    // <ssid length> <ssid...> <password length> <password...>
    if (data.empty()) return false;
    const size_t ssid_len = data[0];
    if (ssid_len == 0 || 1 + ssid_len >= data.size()) return false;
    const size_t pass_len = data[1 + ssid_len];
    if (2 + ssid_len + pass_len > data.size()) return false;
    ssid.assign(data.begin() + 1, data.begin() + 1 + ssid_len);
    password.assign(data.begin() + 2 + ssid_len, data.begin() + 2 + ssid_len + pass_len);
    return true;
}

std::vector<uint8_t> encode_state(State state)
{
    return encode(Type::CurrentState, {static_cast<uint8_t>(state)});
}

std::vector<uint8_t> encode_error(Error error)
{
    return encode(Type::ErrorState, {static_cast<uint8_t>(error)});
}

std::vector<uint8_t> encode_result(Command command, const std::vector<std::string>& strings)
{
    // <command> <strings length> (<length> <string...>)*
    constexpr size_t kMaxStrings = 255 - 2;
    std::vector<uint8_t> data = {static_cast<uint8_t>(command), 0};
    for (const std::string& s : strings) {
        const size_t room = kMaxStrings - (data.size() - 2);
        if (room < 1) break;
        const size_t n = s.size() < room - 1 ? s.size() : room - 1;
        data.push_back(static_cast<uint8_t>(n));
        data.insert(data.end(), s.begin(), s.begin() + n);
    }
    data[1] = static_cast<uint8_t>(data.size() - 2);
    return encode(Type::RpcResult, data);
}

} // namespace app::net::improv

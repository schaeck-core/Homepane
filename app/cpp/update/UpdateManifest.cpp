#include "UpdateManifest.h"

#include <cjson/cJSON.h>

#include <cctype>
#include <cstdlib>
#include <vector>

namespace app::update {

namespace {

std::string get_string(const cJSON* obj, const char* key)
{
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : "";
}

bool is_hex_sha256(const std::string& s)
{
    if (s.size() != 64) return false;
    for (char c : s) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

std::vector<long> numeric_parts(const std::string& v)
{
    std::vector<long> parts;
    size_t i = 0;
    while (i < v.size() && v[i] != '-') {
        if (!std::isdigit(static_cast<unsigned char>(v[i]))) break;
        long n = 0;
        while (i < v.size() && std::isdigit(static_cast<unsigned char>(v[i]))) n = n * 10 + (v[i++] - '0');
        parts.push_back(n);
        if (i < v.size() && v[i] == '.') ++i;
        else break;
    }
    return parts;
}

} // namespace

int compare_versions(const std::string& a, const std::string& b)
{
    std::vector<long> pa = numeric_parts(a), pb = numeric_parts(b);
    size_t n = pa.size() > pb.size() ? pa.size() : pb.size();
    for (size_t i = 0; i < n; ++i) {
        long x = i < pa.size() ? pa[i] : 0, y = i < pb.size() ? pb[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

bool parse_update_manifest(const std::string& json, UpdateManifest& out, std::string& error)
{
    cJSON* root = cJSON_Parse(json.c_str());
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        error = "manifest is not valid JSON";
        return false;
    }

    UpdateManifest m;
    m.version = get_string(root, "version");
    m.min_supported_version = get_string(root, "min_supported_version");
    m.path = get_string(root, "path");
    m.sha256 = get_string(root, "sha256");

    const cJSON* cp = cJSON_GetObjectItemCaseSensitive(root, "coprocessor");
    if (cJSON_IsObject(cp)) {
        m.has_coprocessor = true;
        m.coprocessor_version = get_string(cp, "version");
        m.coprocessor_path = get_string(cp, "path");
        m.coprocessor_sha256 = get_string(cp, "sha256");
    }
    cJSON_Delete(root);

    if (numeric_parts(m.version).empty()) {
        error = "manifest has no valid version";
        return false;
    }
    if (m.path.empty() || !is_hex_sha256(m.sha256)) {
        error = "manifest has no valid firmware path/sha256";
        return false;
    }
    if (m.has_coprocessor &&
        (numeric_parts(m.coprocessor_version).empty() || m.coprocessor_path.empty() ||
         !is_hex_sha256(m.coprocessor_sha256))) {
        error = "manifest has an invalid coprocessor entry";
        return false;
    }
    out = m;
    return true;
}

std::string resolve_url(const std::string& manifest_url, const std::string& relative)
{
    if (relative.rfind("http://", 0) == 0 || relative.rfind("https://", 0) == 0) return relative;
    std::string base = manifest_url;
    size_t q = base.find_first_of("?#");
    if (q != std::string::npos) base.erase(q);
    size_t slash = base.rfind('/');
    size_t scheme = base.find("://");
    if (slash == std::string::npos || (scheme != std::string::npos && slash < scheme + 3)) {
        base += '/';
    } else {
        base.erase(slash + 1);
    }
    std::string rel = relative;
    while (!rel.empty() && rel[0] == '/') rel.erase(0, 1); // stays below the manifest's directory
    return base + rel;
}

} // namespace app::update

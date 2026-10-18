#include "EspUpdateAdapter.h"

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

extern "C" {
#include "slave_ota.h"
}

extern const uint8_t ota_signing_pubkey_pem_start[] asm("_binary_ota_signing_pubkey_pem_start");
extern const uint8_t ota_signing_pubkey_pem_end[] asm("_binary_ota_signing_pubkey_pem_end");

namespace platform_esp32p4 {

namespace {

const char* TAG = "update";
constexpr size_t kChunk = 4096;
constexpr size_t kMaxManifestBytes = 8 * 1024;
constexpr size_t kMaxSignatureBytes = 256;

// RAII around an opened esp_http_client GET.
class HttpGet {
public:
    HttpGet(const std::string& url, std::string& error)
    {
        esp_http_client_config_t cfg = {};
        cfg.url = url.c_str();
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
        cfg.timeout_ms = 15000;
        cfg.buffer_size = 4096;
        cfg.max_redirection_count = 5;
        client_ = esp_http_client_init(&cfg);
        if (!client_) {
            error = "HTTP client init failed";
            return;
        }
        esp_err_t err = esp_http_client_open(client_, 0);
        if (err != ESP_OK) {
            error = std::string("cannot connect: ") + esp_err_to_name(err);
            return;
        }
        length_ = esp_http_client_fetch_headers(client_);
        int status = esp_http_client_get_status_code(client_);
        if (status != 200) {
            error = "HTTP " + std::to_string(status) + " for " + url;
            return;
        }
        ok_ = true;
    }
    ~HttpGet()
    {
        if (client_) {
            esp_http_client_close(client_);
            esp_http_client_cleanup(client_);
        }
    }
    bool ok() const { return ok_; }
    int64_t content_length() const { return length_; } // <= 0 if unknown
    int read(char* buf, int len) { return esp_http_client_read(client_, buf, len); }

private:
    esp_http_client_handle_t client_ = nullptr;
    int64_t length_ = -1;
    bool ok_ = false;
};

bool http_get_small(const std::string& url, size_t limit, std::string& body, std::string& error)
{
    HttpGet get(url, error);
    if (!get.ok()) return false;
    body.clear();
    char buf[512];
    for (;;) {
        int n = get.read(buf, sizeof(buf));
        if (n < 0) {
            error = "read error";
            return false;
        }
        if (n == 0) break;
        body.append(buf, n);
        if (body.size() > limit) {
            error = "response too large";
            return false;
        }
    }
    return true;
}

std::string to_hex(const uint8_t* d, size_t n)
{
    static const char* hex = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) {
        s += hex[d[i] >> 4];
        s += hex[d[i] & 15];
    }
    return s;
}

bool verify_signature(const std::string& data, const std::string& signature, std::string& error)
{
    uint8_t hash[32];
    mbedtls_sha256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), hash, 0);

    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    // EMBED_TXTFILES appends a NUL, which mbedtls requires for PEM input.
    int ret = mbedtls_pk_parse_public_key(&pk, ota_signing_pubkey_pem_start,
                                          ota_signing_pubkey_pem_end - ota_signing_pubkey_pem_start);
    if (ret == 0) {
        ret = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, sizeof(hash),
                                reinterpret_cast<const unsigned char*>(signature.data()), signature.size());
        if (ret != 0) error = "manifest signature is invalid";
    } else {
        error = "embedded public key unusable";
    }
    mbedtls_pk_free(&pk);
    return ret == 0;
}

// Streams `url` to `sink`, hashing everything; fails on length/hash mismatch.
template <typename Sink>
bool download(const std::string& url, size_t max_bytes, const std::string& expected_sha256,
              const std::function<void(size_t done, size_t total)>& on_bytes, Sink sink, std::string& error)
{
    HttpGet get(url, error);
    if (!get.ok()) return false;
    int64_t total = get.content_length();
    if (total > 0 && static_cast<size_t>(total) > max_bytes) {
        error = "image larger than its partition";
        return false;
    }

    std::unique_ptr<char[]> buf(new char[kChunk]);
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    size_t done = 0;
    bool ok = true;
    for (;;) {
        int n = get.read(buf.get(), kChunk);
        if (n < 0) {
            error = "download interrupted";
            ESP_LOGW(TAG, "read failed after %u bytes; internal DMA heap free %u, largest block %u", (unsigned)done,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
            ok = false;
            break;
        }
        if (n == 0) break;
        if (done + n > max_bytes) {
            error = "image larger than its partition";
            ok = false;
            break;
        }
        mbedtls_sha256_update(&sha, reinterpret_cast<unsigned char*>(buf.get()), n);
        if (!sink(buf.get(), static_cast<size_t>(n), done, error)) {
            ok = false;
            break;
        }
        done += n;
        if (on_bytes) on_bytes(done, total > 0 ? static_cast<size_t>(total) : 0);
    }
    uint8_t hash[32];
    mbedtls_sha256_finish(&sha, hash);
    mbedtls_sha256_free(&sha);
    if (!ok) return false;
    if (total > 0 && done != static_cast<size_t>(total)) {
        error = "download incomplete";
        return false;
    }
    if (to_hex(hash, sizeof(hash)) != expected_sha256) {
        error = "SHA-256 mismatch";
        return false;
    }
    return true;
}

} // namespace

bool EspUpdateAdapter::fetch_verified_manifest(const std::string& url, std::string& json, std::string& error)
{
    std::string signature;
    if (!http_get_small(url, kMaxManifestBytes, json, error)) return false;
    if (!http_get_small(url + ".sig", kMaxSignatureBytes, signature, error)) {
        error = "no signature (" + error + ")";
        return false;
    }
    return verify_signature(json, signature, error);
}

bool EspUpdateAdapter::install(const app::update::UpdateManifest& manifest, const std::string& manifest_url,
                               const Progress& progress, std::string& error)
{
    const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
    if (!target) {
        error = "no inactive app slot";
        return false;
    }
    const char* c6_label = slave_ota_c6fw_label_for(target);
    const esp_partition_t* c6_part =
        c6_label ? esp_partition_find_first(ESP_PARTITION_TYPE_DATA, static_cast<esp_partition_subtype_t>(0x40),
                                            c6_label)
                 : nullptr;
    if (manifest.has_coprocessor && !c6_part) {
        error = "no co-processor partition for the inactive slot";
        return false;
    }
    if (!manifest.has_coprocessor && c6_part) {
        // An image left there by an older update would be flashed into the
        // co-processor when the new slot boots; without one the co-processor
        // simply keeps its current firmware.
        esp_err_t e = esp_partition_erase_range(c6_part, 0, c6_part->size);
        if (e != ESP_OK) {
            error = std::string("erase co-processor partition: ") + esp_err_to_name(e);
            return false;
        }
        c6_part = nullptr;
    }
    // The app is most of the download: 0-80 %, co-processor 80-100 %.
    const int app_share = manifest.has_coprocessor ? 80 : 100;

    // 1. App image into the inactive slot (validated by esp_ota_end()).
    esp_ota_handle_t ota = 0;
    esp_err_t err = esp_ota_begin(target, OTA_SIZE_UNKNOWN, &ota);
    if (err != ESP_OK) {
        error = std::string("ota_begin: ") + esp_err_to_name(err);
        return false;
    }
    ESP_LOGI(TAG, "downloading %s into %s", manifest.version.c_str(), target->label);
    bool ok = download(
        app::update::resolve_url(manifest_url, manifest.path), target->size, manifest.sha256,
        [&](size_t done, size_t total) {
            if (total) progress(static_cast<int>(done * app_share / total));
        },
        [&](const char* data, size_t n, size_t, std::string& e) {
            esp_err_t w = esp_ota_write(ota, data, n);
            if (w != ESP_OK) e = std::string("flash write: ") + esp_err_to_name(w);
            return w == ESP_OK;
        },
        error);
    if (!ok) {
        esp_ota_abort(ota);
        return false;
    }
    err = esp_ota_end(ota);
    if (err != ESP_OK) {
        error = std::string("image rejected: ") + esp_err_to_name(err);
        return false;
    }

    // 2. Co-processor container into that slot's c6fw partition.
    if (c6_part) {
        ESP_LOGI(TAG, "downloading co-processor %s into %s", manifest.coprocessor_version.c_str(), c6_part->label);
        err = esp_partition_erase_range(c6_part, 0, c6_part->size);
        if (err != ESP_OK) {
            error = std::string("erase co-processor partition: ") + esp_err_to_name(err);
            return false;
        }
        ok = download(
            app::update::resolve_url(manifest_url, manifest.coprocessor_path), c6_part->size,
            manifest.coprocessor_sha256,
            [&](size_t done, size_t total) {
                if (total) progress(app_share + static_cast<int>(done * (100 - app_share) / total));
            },
            [&](const char* data, size_t n, size_t offset, std::string& e) {
                if (offset == 0 && (n < 4 || std::memcmp(data, "SBC6", 4) != 0)) {
                    e = "co-processor file is not a c6fw container";
                    return false;
                }
                esp_err_t w = esp_partition_write(c6_part, offset, data, n);
                if (w != ESP_OK) e = std::string("flash write: ") + esp_err_to_name(w);
                return w == ESP_OK;
            },
            error);
        if (!ok) {
            // Leave nothing half-written that the old slot's next update
            // check could pick up (it never reads this partition anyway).
            esp_partition_erase_range(c6_part, 0, c6_part->size);
            return false;
        }
    }

    // 3. Only now switch the boot slot. The bootloader rolls back to the
    //    current slot if the new app crashes before confirming itself.
    err = esp_ota_set_boot_partition(target);
    if (err != ESP_OK) {
        error = std::string("set boot partition: ") + esp_err_to_name(err);
        return false;
    }
    progress(100);
    ESP_LOGW(TAG, "update %s installed into %s", manifest.version.c_str(), target->label);
    return true;
}

std::string EspUpdateAdapter::installed_version() const { return esp_app_get_description()->version; }

std::string EspUpdateAdapter::coprocessor_version() const
{
    const char* v = slave_ota_coprocessor_version();
    return v ? v : "";
}

void EspUpdateAdapter::restart() { esp_restart(); }

} // namespace platform_esp32p4

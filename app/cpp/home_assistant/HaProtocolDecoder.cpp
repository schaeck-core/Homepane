#include "HaProtocolDecoder.h"

#include <cjson/cJSON.h>

#include <memory>

namespace app::home_assistant {

namespace {

struct CJsonDeleter {
    void operator()(cJSON* p) const { cJSON_Delete(p); }
};
using CJsonPtr = std::unique_ptr<cJSON, CJsonDeleter>;

std::string print_owned(cJSON* value) {
    if (!value) return {};
    char* txt = cJSON_PrintUnformatted(value);
    if (!txt) return {};
    std::string result = txt;
    cJSON_free(txt);
    return result;
}

// event.a[entity_id] (is_snapshot) or event.c[entity_id] (delta, possibly
// wrapped in a "+" object — CPP_MIGRATION_PLAN.md Phase 0 finding: real
// HA traffic wraps deltas as {"+": {...}}). "c" inside the effective
// payload is a context id/object (string in snapshots, object in
// deltas) — deliberately ignored here, never treated as another delta
// marker (Phase 0 hotspot notes document this ambiguity).
void parse_entity_map(cJSON* map_obj, bool is_snapshot, std::vector<EntityChange>& out) {
    if (!map_obj || !cJSON_IsObject(map_obj)) return;

    cJSON* entity_entry = nullptr;
    cJSON_ArrayForEach(entity_entry, map_obj) {
        if (!entity_entry->string) continue;

        cJSON* effective = entity_entry;
        if (!is_snapshot) {
            cJSON* plus = cJSON_GetObjectItemCaseSensitive(entity_entry, "+");
            if (plus && cJSON_IsObject(plus)) effective = plus;
        }

        EntityChange change;
        change.entity_id = entity_entry->string;
        change.is_full_snapshot = is_snapshot;

        cJSON* s = cJSON_GetObjectItemCaseSensitive(effective, "s");
        if (s && cJSON_IsString(s)) change.state = s->valuestring;

        cJSON* lc = cJSON_GetObjectItemCaseSensitive(effective, "lc");
        if (lc && cJSON_IsNumber(lc)) change.last_changed_epoch_s = lc->valuedouble;

        cJSON* lu = cJSON_GetObjectItemCaseSensitive(effective, "lu");
        if (lu && cJSON_IsNumber(lu)) change.last_updated_epoch_s = lu->valuedouble;

        cJSON* attrs = cJSON_GetObjectItemCaseSensitive(effective, "a");
        if (attrs && cJSON_IsObject(attrs)) {
            cJSON* attr = nullptr;
            cJSON_ArrayForEach(attr, attrs) {
                if (!attr->string) continue;
                AttributeChange ac;
                ac.name = attr->string;
                if (cJSON_IsNull(attr)) {
                    ac.op = AttributeOp::Removed;
                } else {
                    ac.op = AttributeOp::Set;
                    ac.value_json = print_owned(attr);
                }
                change.attributes.push_back(std::move(ac));
            }
        }

        out.push_back(std::move(change));
    }
}

} // namespace

DecodedMessage HaProtocolDecoder::decode(const std::string& raw_json) {
    DecodedMessage msg;

    CJsonPtr root(cJSON_Parse(raw_json.c_str()));
    if (!root) {
        msg.kind = DecodedMessageKind::Malformed;
        msg.malformed_reason = "invalid JSON";
        return msg;
    }

    cJSON* type = cJSON_GetObjectItemCaseSensitive(root.get(), "type");
    if (!type || !cJSON_IsString(type)) {
        msg.kind = DecodedMessageKind::Malformed;
        msg.malformed_reason = "missing or non-string 'type'";
        return msg;
    }
    std::string t = type->valuestring;

    if (t == "auth_required") {
        msg.kind = DecodedMessageKind::AuthRequired;
        return msg;
    }
    if (t == "auth_ok") {
        msg.kind = DecodedMessageKind::AuthOk;
        return msg;
    }
    if (t == "auth_invalid") {
        msg.kind = DecodedMessageKind::AuthInvalid;
        cJSON* m = cJSON_GetObjectItemCaseSensitive(root.get(), "message");
        if (m && cJSON_IsString(m)) msg.auth_invalid_message = m->valuestring;
        return msg;
    }
    if (t == "result") {
        msg.kind = DecodedMessageKind::Result;
        cJSON* id = cJSON_GetObjectItemCaseSensitive(root.get(), "id");
        if (id && cJSON_IsNumber(id)) msg.result.id = id->valueint;
        cJSON* success = cJSON_GetObjectItemCaseSensitive(root.get(), "success");
        msg.result.success = success && cJSON_IsBool(success) && cJSON_IsTrue(success);
        cJSON* result = cJSON_GetObjectItemCaseSensitive(root.get(), "result");
        if (result) msg.result.result_json = print_owned(result);
        cJSON* error = cJSON_GetObjectItemCaseSensitive(root.get(), "error");
        if (error && cJSON_IsObject(error)) {
            cJSON* code = cJSON_GetObjectItemCaseSensitive(error, "code");
            cJSON* emsg = cJSON_GetObjectItemCaseSensitive(error, "message");
            if (code && cJSON_IsString(code)) msg.result.error_code = code->valuestring;
            if (emsg && cJSON_IsString(emsg)) msg.result.error_message = emsg->valuestring;
        }
        return msg;
    }
    if (t == "event") {
        cJSON* event = cJSON_GetObjectItemCaseSensitive(root.get(), "event");
        cJSON* id = cJSON_GetObjectItemCaseSensitive(root.get(), "id");
        int sub_id = (id && cJSON_IsNumber(id)) ? id->valueint : 0;

        if (event && cJSON_IsObject(event)) {
            cJSON* snapshot = cJSON_GetObjectItemCaseSensitive(event, "a");
            if (snapshot && cJSON_IsObject(snapshot)) {
                msg.kind = DecodedMessageKind::EntitySnapshot;
                msg.event_subscription_id = sub_id;
                parse_entity_map(snapshot, true, msg.entity_changes);
                return msg;
            }
            cJSON* delta = cJSON_GetObjectItemCaseSensitive(event, "c");
            if (delta && cJSON_IsObject(delta)) {
                msg.kind = DecodedMessageKind::EntityDelta;
                msg.event_subscription_id = sub_id;
                parse_entity_map(delta, false, msg.entity_changes);
                return msg;
            }
        }
        // Recognized envelope, but not an entity snapshot/delta (e.g. a
        // weather forecast event, CPP_MIGRATION_PLAN.md Phase 9) — pass
        // the raw event body through rather than discarding it.
        if (event && cJSON_IsObject(event)) {
            msg.kind = DecodedMessageKind::RawEvent;
            msg.event_subscription_id = sub_id;
            msg.raw_event_json = print_owned(event);
            return msg;
        }
        msg.kind = DecodedMessageKind::Unknown;
        return msg;
    }

    msg.kind = DecodedMessageKind::Unknown;
    return msg;
}

} // namespace app::home_assistant

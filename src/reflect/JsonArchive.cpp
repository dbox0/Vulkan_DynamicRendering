#include "JsonArchive.h"

#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <optional>
#include <system_error>
#include <utility>

namespace reflect
{
    namespace
    {
        void requireRegistered(const TypeInfo &type)
        {
            if (!type.isRegistered()) {
                fatalError(std::string("reflect: json archive used with unregistered type '") +
                           type.cppName + "'");
            }
        }

        // --------------------------------------------------------------------
        // Numbers
        // --------------------------------------------------------------------

        Json nonFinite(double v)
        {
            if (std::isnan(v)) {
                return "nan";
            }
            return v > 0 ? "inf" : "-inf";
        }

        // JSON numbers are doubles. Written naively, 0.3f becomes
        // 0.30000001192092896. Instead take the shortest text that reads back
        // as the same FLOAT and store that as the double. The final check
        // guards the rare case where rounding through double lands on a
        // different float; then the exact double is written instead.
        Json floatToJson(float f)
        {
            if (!std::isfinite(f)) {
                return nonFinite(f);
            }
            char buffer[32];
            const auto written = std::to_chars(buffer, buffer + sizeof(buffer), f);
            if (written.ec == std::errc{}) {
                double d = 0.0;
                const auto parsed = std::from_chars(buffer, written.ptr, d);
                if (parsed.ec == std::errc{} && static_cast<float>(d) == f &&
                    std::signbit(d) == std::signbit(f)) {
                    return d;
                }
            }
            return static_cast<double>(f);
        }

        Json doubleToJson(double d)
        {
            return std::isfinite(d) ? Json(d) : nonFinite(d);
        }

        std::optional<double> readNumber(const Json &j)
        {
            if (j.is_number()) {
                return j.get<double>();
            }
            if (j.is_string()) {
                const auto &s = j.get_ref<const std::string &>();
                if (s == "nan")  return std::numeric_limits<double>::quiet_NaN();
                if (s == "inf")  return std::numeric_limits<double>::infinity();
                if (s == "-inf") return -std::numeric_limits<double>::infinity();
            }
            return std::nullopt;
        }

        template <class I>
        std::optional<I> readInteger(const Json &j)
        {
            if (j.is_number_unsigned()) {
                const auto v = j.get<uint64_t>();
                return std::in_range<I>(v) ? std::optional<I>(static_cast<I>(v)) : std::nullopt;
            }
            if (j.is_number_integer()) {
                const auto v = j.get<int64_t>();
                return std::in_range<I>(v) ? std::optional<I>(static_cast<I>(v)) : std::nullopt;
            }
            // A hand-edited "3.0" is accepted for 32-bit fields, where every
            // value is exactly representable as a double and the range check
            // below is exact. 64-bit fields must be written as integers.
            if (j.is_number_float() && sizeof(I) <= 4) {
                const auto d = j.get<double>();
                if (std::isfinite(d) && std::trunc(d) == d &&
                    d >= static_cast<double>(std::numeric_limits<I>::min()) &&
                    d <= static_cast<double>(std::numeric_limits<I>::max())) {
                    return static_cast<I>(d);
                }
            }
            return std::nullopt;
        }

        std::optional<Guid> readGuid(const Json &j)
        {
            if (j.is_null()) {
                return Guid{};
            }
            if (j.is_string()) {
                return Guid::parse(j.get_ref<const std::string &>());
            }
            return std::nullopt;
        }

        Json guidToJson(Guid guid)
        {
            return guid.isNull() ? Json(nullptr) : Json(guid.toString());
        }

        // --------------------------------------------------------------------
        // Writer
        // --------------------------------------------------------------------

        Json writeStructure(const TypeInfo &type, const void *object, bool nested);

        template <class T>
        const T &as(const void *p) { return *static_cast<const T *>(p); }

        Json writeValue(const ValueType &type, const void *p)
        {
            switch (type.kind) {
            case ValueKind::Bool:   return as<bool>(p);
            case ValueKind::Int32:  return as<int32_t>(p);
            case ValueKind::UInt32: return as<uint32_t>(p);
            case ValueKind::Int64:  return as<int64_t>(p);
            case ValueKind::UInt64: return as<uint64_t>(p);
            case ValueKind::Float:  return floatToJson(as<float>(p));
            case ValueKind::Double: return doubleToJson(as<double>(p));

            case ValueKind::Vec2:
            {
                const auto &v = as<glm::vec2>(p);
                return Json::array({ floatToJson(v.x), floatToJson(v.y) });
            }
            case ValueKind::Vec3:
            {
                const auto &v = as<glm::vec3>(p);
                return Json::array({ floatToJson(v.x), floatToJson(v.y), floatToJson(v.z) });
            }
            case ValueKind::Vec4:
            {
                const auto &v = as<glm::vec4>(p);
                return Json::array({ floatToJson(v.x), floatToJson(v.y),
                                     floatToJson(v.z), floatToJson(v.w) });
            }
            case ValueKind::Quat:
            {
                const auto &q = as<glm::quat>(p);
                return Json::array({ floatToJson(q.x), floatToJson(q.y),
                                     floatToJson(q.z), floatToJson(q.w) });
            }

            case ValueKind::String:  return as<std::string>(p);
            case ValueKind::Guid:    return guidToJson(as<Guid>(p));
            case ValueKind::NodeRef: return guidToJson(as<NodeRef>(p).guid);

            case ValueKind::Enum:
            {
                const int64_t raw = type.enumType->read(p);
                if (const auto *entry = type.enumType->findByValue(raw)) {
                    return entry->name;
                }
                return raw;
            }

            case ValueKind::Struct:
                return writeStructure(*type.structType, p, true);

            case ValueKind::Array:
            {
                Json out = Json::array();
                const size_t count = type.array.size(p);
                for (size_t i = 0; i < count; ++i) {
                    out.push_back(writeValue(*type.element, type.array.elementConst(p, i)));
                }
                return out;
            }
            }
            return nullptr;
        }

        Json writeStructure(const TypeInfo &type, const void *object, bool nested)
        {
            requireRegistered(type);

            Json out = Json::object();
            if (nested && type.version > 1) {
                out["$version"] = type.version;
            }
            for (const FieldInfo &field : type.fields) {
                if (field.inFiles()) {
                    out[field.name] = writeValue(*field.type, field.get(object));
                }
            }
            return out;
        }

        // --------------------------------------------------------------------
        // Reader
        // --------------------------------------------------------------------

        class Reader
        {
        public:
            explicit Reader(std::vector<std::string> *warnings) : m_warnings(warnings) {}

            void structure(const TypeInfo &type, void *object, const Json &data,
                           uint32_t version, const std::string &path)
            {
                requireRegistered(type);

                if (!data.is_object()) {
                    warn(path, "expected an object");
                    return;
                }
                if (version == 0 || version > type.version) {
                    warn(path, "data has version " + std::to_string(version) + ", code has " +
                               std::to_string(type.version) + "; left unchanged");
                    return;
                }

                const Json *source = &data;
                Json        migrated;
                if (version < type.version && type.migrate) {
                    migrated = data;
                    for (uint32_t v = version; v < type.version; ++v) {
                        type.migrate(migrated, v);
                    }
                    if (!migrated.is_object()) {
                        warn(path, "migrate() did not produce an object; left unchanged");
                        return;
                    }
                    source = &migrated;
                }

                for (const FieldInfo &field : type.fields) {
                    if (!field.inFiles()) {
                        continue;
                    }
                    const auto it = source->find(field.name);
                    if (it != source->end()) {
                        value(*field.type, field.get(object), *it, path + "." + field.name);
                    }
                }

                for (const auto &item : source->items()) {
                    const std::string &key = item.key();
                    if (!key.empty() && key.front() == '$') {
                        continue;
                    }
                    const FieldInfo *field = type.findField(key);
                    if (!field || !field->inFiles()) {
                        warn(path, "unknown field '" + key + "' ignored");
                    }
                }

                if (type.afterRead) {
                    type.afterRead(object);
                }
            }

        private:
            void warn(const std::string &path, const std::string &message)
            {
                if (m_warnings) {
                    m_warnings->push_back(path + ": " + message);
                }
            }

            template <class I>
            void integer(void *p, const Json &j, const std::string &path)
            {
                if (const auto v = readInteger<I>(j)) {
                    *static_cast<I *>(p) = *v;
                } else {
                    warn(path, "expected an integer in range");
                }
            }

            // All N components or nothing: a half-read vector is never applied.
            static std::optional<std::array<float, 4>> floats(const Json &j, size_t n)
            {
                if (!j.is_array() || j.size() != n) {
                    return std::nullopt;
                }
                std::array<float, 4> out{};
                for (size_t i = 0; i < n; ++i) {
                    const auto d = readNumber(j[i]);
                    if (!d) {
                        return std::nullopt;
                    }
                    out[i] = static_cast<float>(*d);
                }
                return out;
            }

            template <class V, int N>
            void vec(void *p, const Json &j, const std::string &path)
            {
                const auto c = floats(j, static_cast<size_t>(N));
                if (!c) {
                    warn(path, "expected an array of " + std::to_string(N) + " numbers");
                    return;
                }
                V &v = *static_cast<V *>(p);
                for (int i = 0; i < N; ++i) {
                    v[i] = (*c)[static_cast<size_t>(i)];
                }
            }

            void value(const ValueType &type, void *p, const Json &j, const std::string &path)
            {
                switch (type.kind) {
                case ValueKind::Bool:
                    if (j.is_boolean()) {
                        *static_cast<bool *>(p) = j.get<bool>();
                    } else {
                        warn(path, "expected a boolean");
                    }
                    return;

                case ValueKind::Int32:  integer<int32_t>(p, j, path);  return;
                case ValueKind::UInt32: integer<uint32_t>(p, j, path); return;
                case ValueKind::Int64:  integer<int64_t>(p, j, path);  return;
                case ValueKind::UInt64: integer<uint64_t>(p, j, path); return;

                case ValueKind::Float:
                    if (const auto d = readNumber(j)) {
                        *static_cast<float *>(p) = static_cast<float>(*d);
                    } else {
                        warn(path, "expected a number");
                    }
                    return;

                case ValueKind::Double:
                    if (const auto d = readNumber(j)) {
                        *static_cast<double *>(p) = *d;
                    } else {
                        warn(path, "expected a number");
                    }
                    return;

                case ValueKind::Vec2: vec<glm::vec2, 2>(p, j, path); return;
                case ValueKind::Vec3: vec<glm::vec3, 3>(p, j, path); return;
                case ValueKind::Vec4: vec<glm::vec4, 4>(p, j, path); return;

                case ValueKind::Quat:
                {
                    // [x, y, z, w]. Named members, not operator[]: glm's index
                    // order for quaternions depends on GLM_FORCE_QUAT_DATA_WXYZ.
                    const auto c = floats(j, 4);
                    if (!c) {
                        warn(path, "expected an array of 4 numbers [x, y, z, w]");
                        return;
                    }
                    glm::quat &q = *static_cast<glm::quat *>(p);
                    q.x = (*c)[0];
                    q.y = (*c)[1];
                    q.z = (*c)[2];
                    q.w = (*c)[3];
                    return;
                }

                case ValueKind::String:
                    if (j.is_string()) {
                        *static_cast<std::string *>(p) = j.get<std::string>();
                    } else {
                        warn(path, "expected a string");
                    }
                    return;

                case ValueKind::Guid:
                    if (const auto g = readGuid(j)) {
                        *static_cast<Guid *>(p) = *g;
                    } else {
                        warn(path, "expected a 16-digit hex id or null");
                    }
                    return;

                case ValueKind::NodeRef:
                    if (const auto g = readGuid(j)) {
                        static_cast<NodeRef *>(p)->guid = *g;
                    } else {
                        warn(path, "expected a 16-digit hex id or null");
                    }
                    return;

                case ValueKind::Enum:
                {
                    const EnumInfo &info = *type.enumType;
                    if (j.is_string()) {
                        if (const auto *entry = info.findByName(j.get_ref<const std::string &>())) {
                            info.write(p, entry->value);
                        } else {
                            warn(path, "unknown " + info.name + " value '" + j.get<std::string>() + "'");
                        }
                    } else if (const auto raw = readInteger<int64_t>(j)) {
                        info.write(p, *raw);
                    } else {
                        warn(path, "expected a " + info.name + " name");
                    }
                    return;
                }

                case ValueKind::Struct:
                {
                    uint32_t version = 1;
                    if (j.is_object()) {
                        if (const auto it = j.find("$version"); it != j.end()) {
                            const auto v = readInteger<uint32_t>(*it);
                            if (!v) {
                                warn(path, "invalid $version");
                                return;
                            }
                            version = *v;
                        }
                    }
                    structure(*type.structType, p, j, version, path);
                    return;
                }

                case ValueKind::Array:
                {
                    if (!j.is_array()) {
                        warn(path, "expected an array");
                        return;
                    }
                    type.array.resize(p, j.size());
                    for (size_t i = 0; i < j.size(); ++i) {
                        value(*type.element, type.array.element(p, i), j[i],
                              path + "[" + std::to_string(i) + "]");
                    }
                    return;
                }
                }
            }

            std::vector<std::string> *m_warnings;
        };
    }

    Json toJson(const TypeInfo &type, const void *object)
    {
        return writeStructure(type, object, false);
    }

    bool fromJson(const TypeInfo &type, void *object, const Json &data,
                  uint32_t dataVersion, std::vector<std::string> *warnings)
    {
        requireRegistered(type);
        if (!data.is_object() || dataVersion == 0 || dataVersion > type.version) {
            return false;
        }
        Reader(warnings).structure(type, object, data, dataVersion, type.name);
        return true;
    }
}

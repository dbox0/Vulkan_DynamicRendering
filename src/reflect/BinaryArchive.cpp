#include "BinaryArchive.h"

#include <cstring>
#include <limits>
#include <string>
#include <type_traits>

namespace reflect
{
    namespace
    {
        constexpr uint32_t fnv1a(std::string_view text)
        {
            uint32_t hash = 2166136261u;
            for (const char c : text) {
                hash ^= static_cast<uint8_t>(c);
                hash *= 16777619u;
            }
            return hash;
        }

        uint32_t serializedFieldCount(const TypeInfo &type)
        {
            uint32_t count = 0;
            for (const FieldInfo &field : type.fields) {
                count += field.inSnapshots() ? 1u : 0u;
            }
            return count;
        }

        void requireRegistered(const TypeInfo &type)
        {
            if (!type.isRegistered()) {
                fatalError(std::string("reflect: binary archive used with unregistered type '") +
                           type.cppName + "'");
            }
        }

        // --------------------------------------------------------------------
        // Writer
        // --------------------------------------------------------------------

        class Writer
        {
        public:
            explicit Writer(Blob &out) : m_out(out) {}

            void structure(const TypeInfo &type, const void *object)
            {
                requireRegistered(type);
                pod(fnv1a(type.name));
                pod(serializedFieldCount(type));
                for (const FieldInfo &field : type.fields) {
                    if (field.inSnapshots()) {
                        value(*field.type, field.get(object));
                    }
                }
            }

        private:
            template <class T>
            void pod(const T &v)
            {
                static_assert(std::is_trivially_copyable_v<T>);
                const size_t at = m_out.size();
                m_out.resize(at + sizeof(T));
                std::memcpy(m_out.data() + at, &v, sizeof(T));
            }

            template <class T>
            const T &as(const void *p) { return *static_cast<const T *>(p); }

            void value(const ValueType &type, const void *p)
            {
                switch (type.kind) {
                case ValueKind::Bool:   pod(static_cast<uint8_t>(as<bool>(p) ? 1 : 0)); break;
                case ValueKind::Int32:  pod(as<int32_t>(p));  break;
                case ValueKind::UInt32: pod(as<uint32_t>(p)); break;
                case ValueKind::Int64:  pod(as<int64_t>(p));  break;
                case ValueKind::UInt64: pod(as<uint64_t>(p)); break;
                case ValueKind::Float:  pod(as<float>(p));    break;
                case ValueKind::Double: pod(as<double>(p));   break;

                // Component by component: glm types may carry SIMD padding.
                case ValueKind::Vec2: { const auto &v = as<glm::vec2>(p); pod(v.x); pod(v.y); break; }
                case ValueKind::Vec3: { const auto &v = as<glm::vec3>(p); pod(v.x); pod(v.y); pod(v.z); break; }
                case ValueKind::Vec4: { const auto &v = as<glm::vec4>(p); pod(v.x); pod(v.y); pod(v.z); pod(v.w); break; }
                case ValueKind::Quat: { const auto &q = as<glm::quat>(p); pod(q.x); pod(q.y); pod(q.z); pod(q.w); break; }

                case ValueKind::String:
                {
                    const auto &s = as<std::string>(p);
                    if (s.size() > std::numeric_limits<uint32_t>::max()) {
                        fatalError("reflect: string too long for a binary archive");
                    }
                    pod(static_cast<uint32_t>(s.size()));
                    const size_t at = m_out.size();
                    m_out.resize(at + s.size());
                    if (!s.empty()) {
                        std::memcpy(m_out.data() + at, s.data(), s.size());
                    }
                    break;
                }

                case ValueKind::Guid:    pod(as<Guid>(p).value);         break;
                case ValueKind::NodeRef: pod(as<NodeRef>(p).guid.value); break;
                case ValueKind::Enum:    pod(type.enumType->read(p));    break;
                case ValueKind::Struct:  structure(*type.structType, p); break;

                case ValueKind::Array:
                {
                    const size_t count = type.array.size(p);
                    if (count > std::numeric_limits<uint32_t>::max()) {
                        fatalError("reflect: array too long for a binary archive");
                    }
                    pod(static_cast<uint32_t>(count));
                    for (size_t i = 0; i < count; ++i) {
                        value(*type.element, type.array.elementConst(p, i));
                    }
                    break;
                }
                }
            }

            Blob &m_out;
        };

        // --------------------------------------------------------------------
        // Reader
        //
        // Runs twice. With apply == false it only walks and checks (object
        // pointers are null throughout); with apply == true it writes. The
        // second pass cannot fail if the first succeeded, because both read
        // the same bytes with the same logic.
        // --------------------------------------------------------------------

        class Reader
        {
        public:
            Reader(std::span<const uint8_t> data, bool apply) : m_data(data), m_apply(apply) {}

            [[nodiscard]] size_t position() const { return m_pos; }

            bool structure(const TypeInfo &type, void *object)
            {
                requireRegistered(type);

                uint32_t hash  = 0;
                uint32_t count = 0;
                if (!pod(hash) || !pod(count)) {
                    return false;
                }
                if (hash != fnv1a(type.name) || count != serializedFieldCount(type)) {
                    return false;
                }

                for (const FieldInfo &field : type.fields) {
                    if (!field.inSnapshots()) {
                        continue;
                    }
                    if (!value(*field.type, m_apply ? field.get(object) : nullptr)) {
                        return false;
                    }
                }

                if (m_apply && type.afterRead) {
                    type.afterRead(object);
                }
                return true;
            }

        private:
            [[nodiscard]] size_t remaining() const { return m_data.size() - m_pos; }

            template <class T>
            bool pod(T &out)
            {
                static_assert(std::is_trivially_copyable_v<T>);
                if (remaining() < sizeof(T)) {
                    return false;
                }
                std::memcpy(&out, m_data.data() + m_pos, sizeof(T));
                m_pos += sizeof(T);
                return true;
            }

            // Reads a T and, when applying, stores it at p.
            template <class T>
            bool scalar(void *p)
            {
                T v{};
                if (!pod(v)) {
                    return false;
                }
                if (m_apply) {
                    *static_cast<T *>(p) = v;
                }
                return true;
            }

            template <class V, int N>
            bool vec(void *p)
            {
                float c[4]{};
                for (int i = 0; i < N; ++i) {
                    if (!pod(c[i])) {
                        return false;
                    }
                }
                if (m_apply) {
                    V &v = *static_cast<V *>(p);
                    for (int i = 0; i < N; ++i) {
                        v[i] = c[i];
                    }
                }
                return true;
            }

            bool value(const ValueType &type, void *p)
            {
                switch (type.kind) {
                case ValueKind::Bool:
                {
                    uint8_t b = 0;
                    if (!pod(b) || b > 1) {
                        return false;
                    }
                    if (m_apply) {
                        *static_cast<bool *>(p) = b != 0;
                    }
                    return true;
                }
                case ValueKind::Int32:  return scalar<int32_t>(p);
                case ValueKind::UInt32: return scalar<uint32_t>(p);
                case ValueKind::Int64:  return scalar<int64_t>(p);
                case ValueKind::UInt64: return scalar<uint64_t>(p);
                case ValueKind::Float:  return scalar<float>(p);
                case ValueKind::Double: return scalar<double>(p);
                case ValueKind::Vec2:   return vec<glm::vec2, 2>(p);
                case ValueKind::Vec3:   return vec<glm::vec3, 3>(p);
                case ValueKind::Vec4:   return vec<glm::vec4, 4>(p);

                case ValueKind::Quat:
                {
                    // Named members, not operator[]: glm's index order for
                    // quaternions depends on GLM_FORCE_QUAT_DATA_WXYZ.
                    float x = 0, y = 0, z = 0, w = 0;
                    if (!pod(x) || !pod(y) || !pod(z) || !pod(w)) {
                        return false;
                    }
                    if (m_apply) {
                        glm::quat &q = *static_cast<glm::quat *>(p);
                        q.x = x; q.y = y; q.z = z; q.w = w;
                    }
                    return true;
                }

                case ValueKind::String:
                {
                    uint32_t length = 0;
                    if (!pod(length) || remaining() < length) {
                        return false;
                    }
                    if (m_apply) {
                        static_cast<std::string *>(p)->assign(
                            reinterpret_cast<const char *>(m_data.data() + m_pos), length);
                    }
                    m_pos += length;
                    return true;
                }

                case ValueKind::Guid:
                {
                    uint64_t v = 0;
                    if (!pod(v)) {
                        return false;
                    }
                    if (m_apply) {
                        static_cast<Guid *>(p)->value = v;
                    }
                    return true;
                }

                case ValueKind::NodeRef:
                {
                    uint64_t v = 0;
                    if (!pod(v)) {
                        return false;
                    }
                    if (m_apply) {
                        static_cast<NodeRef *>(p)->guid.value = v;
                    }
                    return true;
                }

                case ValueKind::Enum:
                {
                    int64_t v = 0;
                    if (!pod(v)) {
                        return false;
                    }
                    if (m_apply) {
                        type.enumType->write(p, v);
                    }
                    return true;
                }

                case ValueKind::Struct:
                    return structure(*type.structType, p);

                case ValueKind::Array:
                {
                    uint32_t count = 0;
                    if (!pod(count)) {
                        return false;
                    }
                    // Every element occupies at least one byte, so a count
                    // larger than what is left is corrupt -- and checking it
                    // here keeps a bad blob from triggering a huge resize().
                    if (count > remaining()) {
                        return false;
                    }
                    if (m_apply) {
                        type.array.resize(p, count);
                    }
                    for (uint32_t i = 0; i < count; ++i) {
                        void *element = m_apply ? type.array.element(p, i) : nullptr;
                        if (!value(*type.element, element)) {
                            return false;
                        }
                    }
                    return true;
                }
                }
                return false;
            }

            std::span<const uint8_t> m_data;
            size_t                   m_pos   = 0;
            bool                     m_apply = false;
        };
    }

    void writeBinary(const TypeInfo &type, const void *object, Blob &out)
    {
        Writer(out).structure(type, object);
    }

    bool readBinaryPrefix(const TypeInfo &type, void *object,
                          std::span<const uint8_t> data, size_t &consumed)
    {
        Reader check(data, false);
        if (!check.structure(type, nullptr)) {
            return false;
        }

        Reader apply(data, true);
        if (!apply.structure(type, object) || apply.position() != check.position()) {
            fatalError("reflect: binary read diverged between validation and apply");
        }

        consumed = apply.position();
        return true;
    }

    bool readBinary(const TypeInfo &type, void *object, std::span<const uint8_t> data)
    {
        // Validate the exact-size requirement before anything is applied, so
        // a blob with trailing bytes also leaves the object untouched.
        Reader check(data, false);
        if (!check.structure(type, nullptr) || check.position() != data.size()) {
            return false;
        }
        size_t consumed = 0;
        return readBinaryPrefix(type, object, data, consumed);
    }
}

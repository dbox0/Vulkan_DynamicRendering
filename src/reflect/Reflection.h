#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json_fwd.hpp>

#include "../common/Fatal.h"
#include "../common/Guid.h"

// ============================================================================
// Runtime type descriptions.
//
// A type describes its fields ONCE, here. Every system that needs to walk an
// object's data -- undo, scene files, the inspector, duplicate, prefabs --
// reads that description instead of knowing the type. Adding a component is
// a registration call and nothing else.
//
//   Types (components, node data) are open.
//   Value kinds (ValueKind below) are closed: every archive has a case per
//   kind. A field of an unsupported C++ type is a compile error
//
//   NAMES ARE THE FORMAT
//   The type name ("Light") and field names ("castsShadows") are what files
//   store. They are deliberately separate from the C++ identifiers, so a C++
//   rename is free and a serialized rename is an explicit migrate() step.
//
//   Dervied state is not a field
//   Cached matrices, dirty flags, GPU handles, slot IDsare left out and rebuilt via afterRead().
//   A runtime handle that undo must restore is  the one exception,
//   and it is flagged RuntimeHandle so no file sees it
//
//   REGISTRATION
//   Call registerX() functions once at startup, before anything serializes
//   then validate().

namespace reflect
{
    using Json = nlohmann::ordered_json;

    enum class ValueKind : uint8_t
    {
        Bool,
        Int32,
        UInt32,
        Int64,
        UInt64,
        Float,
        Double,
        Vec2,
        Vec3,
        Vec4,
        Quat,
        String,
        Guid,
        NodeRef,
        Enum,
        Struct,
        Array
    };

    [[nodiscard]] const char *kindName(ValueKind kind);

    struct TypeInfo;
    struct EnumInfo;
    struct ValueType;

    // Type-erased std::vector<T>. Only vector
    struct ArrayOps
    {
        size_t      (*size)(const void *array)                      = nullptr;
        void        (*resize)(void *array, size_t count)            = nullptr;
        void       *(*element)(void *array, size_t index)           = nullptr;
        const void *(*elementConst)(const void *array, size_t index) = nullptr;
    };

    // How to read and write one value. One instance per C++ type, obtained
    // through valueTypeOf<T>(); compare by address.
    struct ValueType
    {
        ValueKind        kind       = ValueKind::Bool;
        const TypeInfo  *structType = nullptr;   // Struct
        const EnumInfo  *enumType   = nullptr;   // Enum
        const ValueType *element    = nullptr;   // Array
        ArrayOps         array;                  // Array
        const char      *cppName    = "";        // diagnostics only (typeid, mangled)
    };

    namespace FieldFlags
    {
        inline constexpr uint32_t None            = 0;
        // Skipped by every archive: not in undo, not in files. For values the inspector may show but
        // that are derived or runtime-only.

        inline constexpr uint32_t Transient       = 1u << 0;
        inline constexpr uint32_t HideInInspector = 1u << 1;
        inline constexpr uint32_t ReadOnly        = 1u << 2;
        // In-memory snapshots only.
        inline constexpr uint32_t RuntimeHandle   = 1u << 3;
    }

    // Presentation hints for the inspector. Archives ignore them.
    enum class FieldHint : uint8_t
    {
        None,
        Color,      // vec3/vec4 edited as a colour
        Rotation    // quat shown as Euler degrees
    };

    struct FieldInfo
    {
        std::string      name;          // serialized key -- stable
        std::string      displayName;   // inspector label
        std::string      tooltip;
        const ValueType *type   = nullptr;
        void           *(*access)(void *object) = nullptr;
        uint32_t         flags  = FieldFlags::None;
        FieldHint        hint   = FieldHint::None;
        bool             hasRange = false;
        float            rangeMin = 0.0f;
        float            rangeMax = 0.0f;

        [[nodiscard]] void *get(void *object) const { return access(object); }
        [[nodiscard]] const void *get(const void *object) const
        {
            // access() only computes an address; it never writes.
            return access(const_cast<void *>(object));
        }
        // In undo snapshots (BinaryArchive)?
        [[nodiscard]] bool inSnapshots() const { return (flags & FieldFlags::Transient) == 0; }
        // In files (JsonArchive)?
        [[nodiscard]] bool inFiles() const
        {
            return (flags & (FieldFlags::Transient | FieldFlags::RuntimeHandle)) == 0;
        }
    };

    struct TypeInfo
    {
        std::string name;
        // Bump when the serialized shape changes in a way migrate() must
        // handle. Adding a field with a sensible default does NOT need a bump:
        // readers keep defaults for absent keys. 0 means "never registered".
        uint32_t    version = 0;
        size_t      size    = 0;
        size_t      align   = 0;
        const char *cppName = "";
        std::vector<FieldInfo> fields;

        // Heap factory, for types that are instantiated by name (components
        // read from a file). Null for types that live in someone's own storage,
        // like Node in NodeWorld.
        void *(*create)()             = nullptr;
        void  (*destroy)(void *object) = nullptr;

        // After any archive has written into an object: undo restore, file
        // load, duplicate. Rebuild derived state here. Runs for nested structs
        // too, innermost first.
        void (*afterRead)(void *object) = nullptr;

        // JSON only. Rewrites data from fromVersion to fromVersion + 1, in
        // place. Called once per step until the data reaches `version`.
        void (*migrate)(Json &data, uint32_t fromVersion) = nullptr;

        [[nodiscard]] bool isRegistered() const { return version != 0; }
        [[nodiscard]] const FieldInfo *findField(std::string_view fieldName) const;
    };

    struct EnumInfo
    {
        struct Entry
        {
            std::string name;
            int64_t     value = 0;
        };

        std::string        name;
        uint8_t            size     = 0;   // 0 means "never registered"
        bool               isSigned = false;
        const char        *cppName  = "";
        std::vector<Entry> entries;

        [[nodiscard]] bool isRegistered() const { return size != 0; }
        [[nodiscard]] const Entry *findByValue(int64_t value) const;
        [[nodiscard]] const Entry *findByName(std::string_view entryName) const;

        [[nodiscard]] int64_t read(const void *enumObject) const;
        void write(void *enumObject, int64_t value) const;
    };

    // ------------------------------------------------------------------------
    // Registry queries

    [[nodiscard]] const TypeInfo *findType(std::string_view name);
    [[nodiscard]] const EnumInfo *findEnum(std::string_view name);

    // Sorted by name : tests/component work deterministically
    [[nodiscard]] std::vector<const TypeInfo *> allTypes();
    [[nodiscard]] std::vector<const EnumInfo *> allEnums();

    // Checks every registered type for fields whose struct or enum type was
    // never registered, and enums with no entries. Called once after all
    // registration;
    // empty result means the tables are complete.
    [[nodiscard]] std::vector<std::string> validate();

    // "castsShadows" -> "Casts Shadows"
    [[nodiscard]] std::string prettifyName(std::string_view name);

    namespace detail
    {
        template <class T>
        TypeInfo &typeStorage()
        {
            static TypeInfo info;
            return info;
        }

        template <class E>
        EnumInfo &enumStorage()
        {
            static EnumInfo info;
            return info;
        }

        void addTypeName(TypeInfo &info);
        void addEnumName(EnumInfo &info);
        void checkName(std::string_view name, std::string_view what);

        // The closed vocabulary.
        template <class T> struct ScalarKind { static constexpr bool supported = false; };

#define REFLECT_SCALAR(Type, Kind)                                                  \
        template <> struct ScalarKind<Type>                                        \
        {                                                                          \
            static constexpr bool      supported = true;                           \
            static constexpr ValueKind kind      = ValueKind::Kind;                \
        };
        REFLECT_SCALAR(bool,        Bool)
        REFLECT_SCALAR(int32_t,     Int32)
        REFLECT_SCALAR(uint32_t,    UInt32)
        REFLECT_SCALAR(int64_t,     Int64)
        REFLECT_SCALAR(uint64_t,    UInt64)
        REFLECT_SCALAR(float,       Float)
        REFLECT_SCALAR(double,      Double)
        REFLECT_SCALAR(glm::vec2,   Vec2)
        REFLECT_SCALAR(glm::vec3,   Vec3)
        REFLECT_SCALAR(glm::vec4,   Vec4)
        REFLECT_SCALAR(glm::quat,   Quat)
        REFLECT_SCALAR(std::string, String)
        REFLECT_SCALAR(::Guid,      Guid)
        REFLECT_SCALAR(::NodeRef,   NodeRef)
#undef REFLECT_SCALAR

        template <class T> struct IsVector : std::false_type {};
        template <class U, class A> struct IsVector<std::vector<U, A>> : std::true_type
        {
            using Element = U;
        };

        // glm types that are not in the vocabulary (dvec3, ivec2, mat4, ...)
        // are classes, and would otherwise fall through to "struct" and only
        // fail at validate()
        template <class T> struct IsGlm : std::false_type {};
        template <glm::length_t L, class T, glm::qualifier Q>
        struct IsGlm<glm::vec<L, T, Q>> : std::true_type {};
        template <glm::length_t C, glm::length_t R, class T, glm::qualifier Q>
        struct IsGlm<glm::mat<C, R, T, Q>> : std::true_type {};
        template <class T, glm::qualifier Q>
        struct IsGlm<glm::qua<T, Q>> : std::true_type {};

        template <class P> struct MemberPointer;
        template <class C, class M> struct MemberPointer<M C::*>
        {
            using Class  = C;
            using Member = M;
        };

        template <class T>
        ValueType makeValueType();
    }

    namespace detail
    {
        // Keyed on the unqualified type, so float, const float and float&
        // share one instance and the "compare by address" promise holds.
        template <class T>
        const ValueType &valueTypeStorage()
        {
            static const ValueType type = makeValueType<T>();
            return type;
        }
    }

    template <class T>
    const ValueType &valueTypeOf() { return detail::valueTypeStorage<std::remove_cvref_t<T>>(); }

    template <class T>
    const TypeInfo &typeOf() { return detail::typeStorage<std::remove_cvref_t<T>>(); }

    template <class E>
    const EnumInfo &enumOf() { return detail::enumStorage<std::remove_cvref_t<E>>(); }

    namespace detail
    {
        template <class T>
        ValueType makeValueType()
        {
            ValueType type;
            type.cppName = typeid(T).name();

            if constexpr (ScalarKind<T>::supported) {
                type.kind = ScalarKind<T>::kind;
            } else if constexpr (std::is_enum_v<T>) {
                static_assert(sizeof(T) <= sizeof(int64_t));
                type.kind     = ValueKind::Enum;
                type.enumType = &enumStorage<T>();
            } else if constexpr (IsVector<T>::value) {
                using E = typename IsVector<T>::Element;
                // vector<bool> has no addressable elements.
                static_assert(!std::is_same_v<E, bool>,
                              "reflect: std::vector<bool> is not supported; use std::vector<uint32_t> or a struct");
                type.kind    = ValueKind::Array;
                type.element = &valueTypeOf<E>();
                type.array.size = [](const void *a) -> size_t {
                    return static_cast<const T *>(a)->size();
                };
                type.array.resize = [](void *a, size_t n) {
                    static_cast<T *>(a)->resize(n);
                };
                type.array.element = [](void *a, size_t i) -> void * {
                    return std::addressof((*static_cast<T *>(a))[i]);
                };
                type.array.elementConst = [](const void *a, size_t i) -> const void * {
                    return std::addressof((*static_cast<const T *>(a))[i]);
                };
            } else {
                static_assert(!IsGlm<T>::value,
                              "reflect: this glm type is not in the value vocabulary (see ValueKind)");
                static_assert(std::is_class_v<T> && !std::is_pointer_v<T>,
                              "reflect: unsupported field type. Pointers, references, raw arrays and "
                              "unlisted scalars are not serializable; add a ValueKind if it is truly needed");
                type.kind       = ValueKind::Struct;
                type.structType = &typeStorage<T>();
            }
            return type;
        }
    }

    // ========================================================================
    // Registration

    template <class T>
    class TypeBuilder
    {
    public:
        explicit TypeBuilder(TypeInfo &info) : m_info(info) {}

        // Usage:  .field<&Light::color>("color")
        //
        // The member pointer is a template argument rather than a function
        // argument so the accessor below can be a plain function pointer

        template <auto Member>
        TypeBuilder &field(std::string_view name, uint32_t flags = FieldFlags::None)
        {
            using Pointer = std::remove_cv_t<decltype(Member)>;
            static_assert(std::is_member_object_pointer_v<Pointer>,
                          "reflect: field<> takes a pointer to a data member");
            using Owner = typename detail::MemberPointer<Pointer>::Class;
            using Value = typename detail::MemberPointer<Pointer>::Member;
            static_assert(std::is_base_of_v<Owner, T>, "reflect: member does not belong to this type");
            static_assert(!std::is_const_v<Value>, "reflect: const members cannot be read into");
            static_assert(!std::is_reference_v<Value>, "reflect: reference members are not supported");

            detail::checkName(name, "field");
            if (m_info.findField(name)) {
                fatalError("reflect: duplicate field '" + std::string(name) + "' on type '" + m_info.name + "'");
            }

            FieldInfo info;
            info.name        = name;
            info.displayName = prettifyName(name);
            info.type        = &valueTypeOf<Value>();
            info.flags       = flags;
            info.access      = [](void *object) -> void * {
                return std::addressof(static_cast<T *>(object)->*Member);
            };
            m_info.fields.push_back(std::move(info));
            return *this;
        }

        // The modifiers below apply to the most recently added field.
        TypeBuilder &label(std::string_view text)   { last().displayName = text; return *this; }
        TypeBuilder &tooltip(std::string_view text) { last().tooltip = text;     return *this; }
        TypeBuilder &hint(FieldHint value)          { last().hint = value;       return *this; }
        TypeBuilder &range(float lo, float hi)
        {
            FieldInfo &f = last();
            f.hasRange = true;
            f.rangeMin = lo;
            f.rangeMax = hi;
            return *this;
        }

        template <auto Callback>
        TypeBuilder &afterRead()
        {
            m_info.afterRead = [](void *object) {
                std::invoke(Callback, *static_cast<T *>(object));
            };
            return *this;
        }

        TypeBuilder &migrate(void (*function)(Json &data, uint32_t fromVersion))
        {
            m_info.migrate = function;
            return *this;
        }

    private:
        FieldInfo &last()
        {
            if (m_info.fields.empty()) {
                fatalError("reflect: field modifier used before any field on '" + m_info.name + "'");
            }
            return m_info.fields.back();
        }

        TypeInfo &m_info;
    };

    template <class E>
    class EnumBuilder
    {
    public:
        explicit EnumBuilder(EnumInfo &info) : m_info(info) {}

        EnumBuilder &value(std::string_view name, E enumValue)
        {
            detail::checkName(name, "enum entry");
            const auto raw = static_cast<int64_t>(static_cast<std::underlying_type_t<E>>(enumValue));
            if (m_info.findByName(name) || m_info.findByValue(raw)) {
                fatalError("reflect: duplicate entry '" + std::string(name) + "' in enum '" + m_info.name + "'");
            }
            m_info.entries.push_back({ std::string(name), raw });
            return *this;
        }

    private:
        EnumInfo &m_info;
    };

    template <class T>
    TypeBuilder<T> registerType(std::string_view name, uint32_t version)
    {
        static_assert(std::is_same_v<T, std::remove_cvref_t<T>>);
        static_assert(std::is_class_v<T>);

        TypeInfo &info = detail::typeStorage<T>();
        if (info.isRegistered()) {
            fatalError("reflect: type registered twice as '" + std::string(name) +
                       "' (already '" + info.name + "')");
        }
        if (version == 0) {
            fatalError("reflect: type '" + std::string(name) + "' needs version >= 1");
        }

        info.name    = name;
        info.version = version;
        info.size    = sizeof(T);
        info.align   = alignof(T);
        info.cppName = typeid(T).name();

        if (std::is_default_constructible_v<T> && std::is_destructible_v<T>) {
            info.create  = []() -> void * { return new T(); };
            info.destroy = [](void *object) { delete static_cast<T *>(object); };
        }

        detail::addTypeName(info);
        return TypeBuilder<T>(info);
    }

    template <class E>
    EnumBuilder<E> registerEnum(std::string_view name)
    {
        static_assert(std::is_enum_v<E>);

        EnumInfo &info = detail::enumStorage<E>();
        if (info.isRegistered()) {
            fatalError("reflect: enum registered twice as '" + std::string(name) + "'");
        }
        info.name     = name;
        info.size     = static_cast<uint8_t>(sizeof(E));
        info.isSigned = std::is_signed_v<std::underlying_type_t<E>>;
        info.cppName  = typeid(E).name();

        detail::addEnumName(info);
        return EnumBuilder<E>(info);
    }

    template <class T>
    [[nodiscard]] bool isRegistered() { return typeOf<T>().isRegistered(); }
}

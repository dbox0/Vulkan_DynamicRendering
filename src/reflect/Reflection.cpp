#include "Reflection.h"

#include <cctype>
#include <cstring>
#include <map>

namespace reflect
{
    namespace
    {
        struct Registry
        {
            std::map<std::string, TypeInfo *, std::less<>> types;
            std::map<std::string, EnumInfo *, std::less<>> enums;
        };

        Registry &registry()
        {
            static Registry instance;
            return instance;
        }

        void checkValueType(const ValueType &type, const std::string &path,
                            std::vector<std::string> &errors)
        {
            switch (type.kind) {
            case ValueKind::Struct:
                if (!type.structType || !type.structType->isRegistered()) {
                    errors.push_back(path + ": struct type '" + type.cppName +
                                     "' is used as a field but was never registered");
                }
                break;
            case ValueKind::Enum:
                if (!type.enumType || !type.enumType->isRegistered()) {
                    errors.push_back(path + ": enum '" + type.cppName +
                                     "' is used as a field but was never registered");
                }
                break;
            case ValueKind::Array:
                if (!type.element) {
                    errors.push_back(path + ": array without an element type");
                } else {
                    checkValueType(*type.element, path + "[]", errors);
                }
                break;
            default:
                break;
            }
        }
    }

    const char *kindName(ValueKind kind)
    {
        switch (kind) {
        case ValueKind::Bool:    return "Bool";
        case ValueKind::Int32:   return "Int32";
        case ValueKind::UInt32:  return "UInt32";
        case ValueKind::Int64:   return "Int64";
        case ValueKind::UInt64:  return "UInt64";
        case ValueKind::Float:   return "Float";
        case ValueKind::Double:  return "Double";
        case ValueKind::Vec2:    return "Vec2";
        case ValueKind::Vec3:    return "Vec3";
        case ValueKind::Vec4:    return "Vec4";
        case ValueKind::Quat:    return "Quat";
        case ValueKind::String:  return "String";
        case ValueKind::Guid:    return "Guid";
        case ValueKind::NodeRef: return "NodeRef";
        case ValueKind::Enum:    return "Enum";
        case ValueKind::Struct:  return "Struct";
        case ValueKind::Array:   return "Array";
        }
        return "?";
    }

    const FieldInfo *TypeInfo::findField(std::string_view fieldName) const
    {
        for (const FieldInfo &field : fields) {
            if (field.name == fieldName) {
                return &field;
            }
        }
        return nullptr;
    }

    const EnumInfo::Entry *EnumInfo::findByValue(int64_t value) const
    {
        for (const Entry &entry : entries) {
            if (entry.value == value) {
                return &entry;
            }
        }
        return nullptr;
    }

    const EnumInfo::Entry *EnumInfo::findByName(std::string_view entryName) const
    {
        for (const Entry &entry : entries) {
            if (entry.name == entryName) {
                return &entry;
            }
        }
        return nullptr;
    }

    // memcpy through fixed-width integers: the enum's underlying type is only
    // known as (size, signedness) at this point.
    int64_t EnumInfo::read(const void *enumObject) const
    {
        switch (size) {
        case 1: { if (isSigned) { int8_t  v; std::memcpy(&v, enumObject, 1); return v; }
                  uint8_t  v; std::memcpy(&v, enumObject, 1); return v; }
        case 2: { if (isSigned) { int16_t v; std::memcpy(&v, enumObject, 2); return v; }
                  uint16_t v; std::memcpy(&v, enumObject, 2); return v; }
        case 4: { if (isSigned) { int32_t v; std::memcpy(&v, enumObject, 4); return v; }
                  uint32_t v; std::memcpy(&v, enumObject, 4); return v; }
        case 8: { int64_t v; std::memcpy(&v, enumObject, 8); return v; }
        default: fatalError("reflect: enum '" + name + "' has an unsupported size");
        }
    }

    void EnumInfo::write(void *enumObject, int64_t value) const
    {
        switch (size) {
        case 1: { const auto v = static_cast<uint8_t>(value);  std::memcpy(enumObject, &v, 1); return; }
        case 2: { const auto v = static_cast<uint16_t>(value); std::memcpy(enumObject, &v, 2); return; }
        case 4: { const auto v = static_cast<uint32_t>(value); std::memcpy(enumObject, &v, 4); return; }
        case 8: { std::memcpy(enumObject, &value, 8); return; }
        default: fatalError("reflect: enum '" + name + "' has an unsupported size");
        }
    }

    const TypeInfo *findType(std::string_view name)
    {
        const auto &types = registry().types;
        const auto  it    = types.find(name);
        return it == types.end() ? nullptr : it->second;
    }

    const EnumInfo *findEnum(std::string_view name)
    {
        const auto &enums = registry().enums;
        const auto  it    = enums.find(name);
        return it == enums.end() ? nullptr : it->second;
    }

    std::vector<const TypeInfo *> allTypes()
    {
        std::vector<const TypeInfo *> out;
        out.reserve(registry().types.size());
        for (const auto &[name, info] : registry().types) {
            out.push_back(info);
        }
        return out;
    }

    std::vector<const EnumInfo *> allEnums()
    {
        std::vector<const EnumInfo *> out;
        out.reserve(registry().enums.size());
        for (const auto &[name, info] : registry().enums) {
            out.push_back(info);
        }
        return out;
    }

    std::vector<std::string> validate()
    {
        std::vector<std::string> errors;
        for (const auto &[name, type] : registry().types) {
            for (const FieldInfo &field : type->fields) {
                if (!field.type || !field.access) {
                    errors.push_back(name + "." + field.name + ": incomplete field");
                    continue;
                }
                checkValueType(*field.type, name + "." + field.name, errors);
            }
        }
        for (const auto &[name, info] : registry().enums) {
            if (info->entries.empty()) {
                errors.push_back("enum " + name + ": has no entries");
            }
        }
        return errors;
    }

    std::string prettifyName(std::string_view name)
    {
        std::string out;
        out.reserve(name.size() + 4);
        for (size_t i = 0; i < name.size(); ++i) {
            const auto c = static_cast<unsigned char>(name[i]);
            if (c == '_') {
                if (!out.empty() && out.back() != ' ') {
                    out.push_back(' ');
                }
                continue;
            }
            if (i > 0 && std::isupper(c)) {
                const auto prev = static_cast<unsigned char>(name[i - 1]);
                if (std::islower(prev) || std::isdigit(prev)) {
                    out.push_back(' ');
                }
            }
            out.push_back(out.empty() || out.back() == ' '
                              ? static_cast<char>(std::toupper(c))
                              : static_cast<char>(c));
        }
        return out;
    }

    namespace detail
    {
        // Letters, digits, '_' and '.'. No leading '$': keys starting with '$'
        // are reserved for archive metadata ("$version").
        void checkName(std::string_view name, std::string_view what)
        {
            bool ok = !name.empty();
            for (const char c : name) {
                const auto u = static_cast<unsigned char>(c);
                ok = ok && (std::isalnum(u) || c == '_' || c == '.');
            }
            if (!ok) {
                fatalError("reflect: invalid " + std::string(what) + " name '" + std::string(name) + "'");
            }
        }

        void addTypeName(TypeInfo &info)
        {
            checkName(info.name, "type");
            auto &types = registry().types;
            if (types.contains(info.name)) {
                fatalError("reflect: two C++ types registered under the name '" + info.name + "'");
            }
            types.emplace(info.name, &info);
        }

        void addEnumName(EnumInfo &info)
        {
            checkName(info.name, "enum");
            auto &enums = registry().enums;
            if (enums.contains(info.name)) {
                fatalError("reflect: two C++ enums registered under the name '" + info.name + "'");
            }
            enums.emplace(info.name, &info);
        }
    }
}

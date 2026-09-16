#pragma once
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Reflection.h"

// ============================================================================
// Text form of reflected objects. What scene files will be made of.
//
// SHAPE
//   An object is a JSON object keyed by field name, in declaration order
//   (ordered_json), so files read naturally and an unchanged object writes the
//   same text every time.
//
//     vec2/3/4    [x, y, z]
//     quat        [x, y, z, w]          glTF order
//     float       shortest text that reads back to the same float; non-finite
//                 values as "nan", "inf", "-inf"
//     enum        entry name; an integer only if the value has no name
//     Guid        16 hex digits, null as JSON null
//     NodeRef     same as Guid
//     array       JSON array
//     struct      nested object, with "$version" when that type's version > 1
//
// VERSIONING
//   The top-level object's version belongs to whoever stores it -- the scene
//   file keeps it next to the component type name -- and is passed to
//   fromJson(). Nested structs carry their own "$version" (absent means 1),
//   so a struct shared by many components can migrate on its own.
//
// TOLERANT READING
//   Missing key          -> the field keeps its current value
//   Wrong JSON type      -> the field keeps its value, a warning is recorded
//   Unknown key          -> ignored, a warning is recorded
//   Unknown enum name    -> the field keeps its value, a warning is recorded
//   Data newer than code -> fromJson() fails and leaves the object untouched;
//                           the caller must keep the raw JSON rather than
//                           lose it (see the scene-file notes)
//
// Warnings carry a path like "Light.color[1]" so a broken file points at the
// broken value.
// ============================================================================

namespace reflect
{
    [[nodiscard]] Json toJson(const TypeInfo &type, const void *object);

    // `dataVersion` is the version the data was written with. Returns false,
    // without touching the object, only when the data is not a JSON object or
    // was written by a newer version of the type. Everything else is best
    // effort and reported through `warnings` (may be null).
    [[nodiscard]] bool fromJson(const TypeInfo &type, void *object, const Json &data,
                                uint32_t dataVersion, std::vector<std::string> *warnings = nullptr);

    template <class T>
    [[nodiscard]] Json toJson(const T &object)
    {
        return toJson(typeOf<T>(), &object);
    }

    template <class T>
    [[nodiscard]] bool fromJson(T &object, const Json &data, uint32_t dataVersion,
                                std::vector<std::string> *warnings = nullptr)
    {
        return fromJson(typeOf<T>(), &object, data, dataVersion, warnings);
    }
}

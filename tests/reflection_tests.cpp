// Guid, reflect:: and NodeWorld identity.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

#include "TestFramework.h"

#include "../src/common/Guid.h"
#include "../src/reflect/BinaryArchive.h"
#include "../src/reflect/JsonArchive.h"
#include "../src/reflect/Reflection.h"
#include "../src/scene/Geometry/NodeWorld.h"
#include "../src/scene/SceneTypes.h"
#include "../src/assets/AssetTypes.h"
#include "../src/assets/Material.h"

namespace
{
    bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

    // ------------------------------------------------------------------------
    // Test types. One of every value kind, nesting, arrays of structs, and a
    // non-default underlying enum type.
    // ------------------------------------------------------------------------

    enum class Mood : int16_t { Calm = -3, Angry = 7, Sleepy = 300 };

    struct Waypoint
    {
        glm::vec3 position{ 0.0f };
        float     wait = 1.0f;
        NodeRef   lookAt;
        int       restoredCount = 0;   // not reflected: counts afterRead calls
    };

    struct Everything
    {
        bool        flag    = false;
        int32_t     i32     = 0;
        uint32_t    u32     = 0;
        int64_t     i64     = 0;
        uint64_t    u64     = 0;
        float       f       = 0.0f;
        double      d       = 0.0;
        glm::vec2   v2{ 0.0f };
        glm::vec3   v3{ 0.0f };
        glm::vec4   v4{ 0.0f };
        glm::quat   q{ 1.0f, 0.0f, 0.0f, 0.0f };
        std::string text;
        Guid        id;
        NodeRef     target;
        Mood        mood = Mood::Calm;
        Waypoint    home;
        std::vector<Waypoint>    route;
        std::vector<std::string> tags;
        std::vector<std::vector<int32_t>> grid;
        float       cachedLength = 0.0f;   // Transient
        uint32_t    gpuHandle    = 0;      // RuntimeHandle
    };

    struct Unregistered { int32_t x = 0; };
    struct HasUnregistered { Unregistered inner; };

    // v1 stored "speed"; v2 renamed it to "velocity".
    struct Versioned
    {
        float velocity = 0.0f;
        float extra    = 5.0f;
    };

    void migrateVersioned(reflect::Json &data, uint32_t from)
    {
        if (from == 1 && data.contains("speed")) {
            data["velocity"] = data["speed"];
            data.erase("speed");
        }
    }

    void registerTestTypes()
    {
        reflect::registerEnum<Mood>("Mood")
            .value("Calm", Mood::Calm)
            .value("Angry", Mood::Angry)
            .value("Sleepy", Mood::Sleepy);

        reflect::registerType<Waypoint>("Waypoint", 2)
            .field<&Waypoint::position>("position")
            .field<&Waypoint::wait>("wait")
            .field<&Waypoint::lookAt>("lookAt")
            .afterRead<[](Waypoint &w) { ++w.restoredCount; }>();

        reflect::registerType<Everything>("Everything", 1)
            .field<&Everything::flag>("flag")
            .field<&Everything::i32>("i32")
            .field<&Everything::u32>("u32")
            .field<&Everything::i64>("i64")
            .field<&Everything::u64>("u64")
            .field<&Everything::f>("f")
            .field<&Everything::d>("d")
            .field<&Everything::v2>("v2")
            .field<&Everything::v3>("v3").hint(reflect::FieldHint::Color)
            .field<&Everything::v4>("v4")
            .field<&Everything::q>("q")
            .field<&Everything::text>("text")
            .field<&Everything::id>("id")
            .field<&Everything::target>("target")
            .field<&Everything::mood>("mood")
            .field<&Everything::home>("home")
            .field<&Everything::route>("route")
            .field<&Everything::tags>("tags")
            .field<&Everything::grid>("grid")
            .field<&Everything::cachedLength>("cachedLength", reflect::FieldFlags::Transient)
            .field<&Everything::gpuHandle>("gpuHandle", reflect::FieldFlags::RuntimeHandle);

        reflect::registerType<Versioned>("Versioned", 2)
            .field<&Versioned::velocity>("velocity")
            .field<&Versioned::extra>("extra")
            .migrate(&migrateVersioned);
    }

    Everything makeFilled()
    {
        Everything e;
        e.flag   = true;
        e.i32    = -123456;
        e.u32    = 4000000000u;
        e.i64    = std::numeric_limits<int64_t>::min();
        e.u64    = std::numeric_limits<uint64_t>::max();
        e.f      = 0.3f;
        e.d      = 1.0 / 3.0;
        e.v2     = { 1.5f, -2.25f };
        e.v3     = { 0.1f, 0.2f, 0.3f };
        e.v4     = { 1.0f, 2.0f, 3.0f, 4.0f };
        e.q      = glm::normalize(glm::quat(0.9f, 0.1f, 0.2f, 0.3f));
        e.text   = "Sun \"light\"\n\xC3\xA9";   // quotes, newline, UTF-8
        e.id     = Guid{ 0x0123456789abcdefull };
        e.target = NodeRef{ Guid{ 42 } };
        e.mood   = Mood::Sleepy;
        e.home   = { { 1.0f, 2.0f, 3.0f }, 0.5f, NodeRef{ Guid{ 7 } } };
        e.route  = { { { 4.0f, 5.0f, 6.0f }, 2.0f, {} }, { { -1.0f, 0.0f, 1.0f }, 0.0f, NodeRef{ Guid{ 9 } } } };
        e.tags   = { "enemy", "", "boss" };
        e.grid   = { { 1, 2 }, {}, { 3 } };
        e.cachedLength = 99.0f;
        e.gpuHandle    = 17;
        return e;
    }

    bool equalWaypoint(const Waypoint &a, const Waypoint &b)
    {
        return a.position == b.position && sameBits(a.wait, b.wait) && a.lookAt == b.lookAt;
    }

    bool equalEverything(const Everything &a, const Everything &b)
    {
        if (a.route.size() != b.route.size()) return false;
        for (size_t i = 0; i < a.route.size(); ++i) {
            if (!equalWaypoint(a.route[i], b.route[i])) return false;
        }
        return a.flag == b.flag && a.i32 == b.i32 && a.u32 == b.u32 && a.i64 == b.i64 &&
               a.u64 == b.u64 && sameBits(a.f, b.f) && a.d == b.d && a.v2 == b.v2 &&
               a.v3 == b.v3 && a.v4 == b.v4 && a.q.x == b.q.x && a.q.y == b.q.y &&
               a.q.z == b.q.z && a.q.w == b.q.w && a.text == b.text && a.id == b.id &&
               a.target == b.target && a.mood == b.mood && equalWaypoint(a.home, b.home) &&
               a.tags == b.tags && a.grid == b.grid && a.gpuHandle == b.gpuHandle;
    }

    bool hasWarning(const std::vector<std::string> &warnings, const std::string &needle)
    {
        for (const auto &w : warnings) {
            if (w.find(needle) != std::string::npos) return true;
        }
        return false;
    }

    // ------------------------------------------------------------------------

    void testGuid()
    {
        section("guid");

        std::unordered_set<Guid> seen;
        bool anyNull = false;
        for (int i = 0; i < 100000; ++i) {
            const Guid g = Guid::generate();
            anyNull |= g.isNull();
            seen.insert(g);
        }
        CHECK(!anyNull);
        CHECK(seen.size() == 100000);

        const Guid g{ 0x00ab00cd00ef0012ull };
        CHECK(g.toString() == "00ab00cd00ef0012");
        CHECK(Guid::parse(g.toString()) == g);
        CHECK(Guid{}.toString() == "0000000000000000");
        CHECK(Guid::parse("ffffffffffffffff")->value == std::numeric_limits<uint64_t>::max());

        CHECK(!Guid::parse(""));
        CHECK(!Guid::parse("00ab00cd00ef001"));     // 15 digits
        CHECK(!Guid::parse("00ab00cd00ef00123"));   // 17 digits
        CHECK(!Guid::parse("00AB00CD00EF0012"));    // uppercase is not what we write
        CHECK(!Guid::parse("00ab00cd00ef001g"));
        CHECK(!Guid::parse("+0ab00cd00ef0012"));
        CHECK(!Guid::parse("0x0b00cd00ef0012"));
    }

    void testRegistry()
    {
        section("registry");

        CHECK(reflect::findType("Everything") == &reflect::typeOf<Everything>());
        CHECK(reflect::findType("Node") == &reflect::typeOf<Node>());
        CHECK(reflect::findType("Nope") == nullptr);
        CHECK(reflect::findEnum("Mood") == &reflect::enumOf<Mood>());
        CHECK(reflect::isRegistered<Everything>());
        CHECK(!reflect::isRegistered<Unregistered>());

        const auto types = reflect::allTypes();
        for (size_t i = 1; i < types.size(); ++i) {
            CHECK(types[i - 1]->name < types[i]->name);
        }

        const auto &info = reflect::typeOf<Everything>();
        CHECK(info.fields.size() == 21);
        CHECK(info.findField("v3")->hint == reflect::FieldHint::Color);
        CHECK(!info.findField("cachedLength")->inSnapshots());
        CHECK(!info.findField("cachedLength")->inFiles());
        CHECK(info.findField("gpuHandle")->inSnapshots());
        CHECK(!info.findField("gpuHandle")->inFiles());
        CHECK(info.create != nullptr);

        // Factory produces a default object.
        void *made = info.create();
        CHECK(static_cast<Everything *>(made)->u32 == 0);
        info.destroy(made);

        const auto &node = reflect::typeOf<Node>();
        CHECK(node.findField("translation") != nullptr);
        CHECK(node.findField("guid") == nullptr);
        CHECK(node.findField("lightIntensity")->hasRange);
        CHECK(node.findField("lightCastsShadows")->displayName == "Light Casts Shadows");

        CHECK(reflect::prettifyName("castsShadows") == "Casts Shadows");
        CHECK(reflect::prettifyName("baseColor2D") == "Base Color2 D");
        CHECK(reflect::prettifyName("snake_case") == "Snake Case");
        CHECK(reflect::prettifyName("x") == "X");

        // Same C++ type -> same ValueType instance.
        CHECK(&reflect::valueTypeOf<float>() == &reflect::valueTypeOf<const float &>());
        CHECK(reflect::valueTypeOf<std::vector<Waypoint>>().element->structType ==
              &reflect::typeOf<Waypoint>());
    }

    void testBinaryRoundTrip()
    {
        section("binary round trip");

        const Everything source = makeFilled();
        const reflect::Blob blob = reflect::toBlob(source);

        Everything target;
        target.cachedLength = -1.0f;
        CHECK(reflect::readBinary(target, blob));
        CHECK(equalEverything(source, target));
        CHECK(target.cachedLength == -1.0f);          // transient: untouched
        CHECK(target.gpuHandle == 17);                 // runtime handle: restored
        CHECK(target.home.restoredCount == 1);         // afterRead on nested struct
        CHECK(target.route[0].restoredCount == 1);     // ... and on array elements
        CHECK(target.route[1].restoredCount == 1);

        // Determinism: same values -> same bytes; the copy re-serializes identically.
        CHECK(reflect::toBlob(target) == blob);
        CHECK(reflect::toBlob(source) == blob);

        // Any change shows up in the bytes.
        Everything changed = source;
        changed.route[1].wait = 0.0001f;
        CHECK(reflect::toBlob(changed) != blob);

        // Transient changes do not.
        changed = source;
        changed.cachedLength = 12345.0f;
        CHECK(reflect::toBlob(changed) == blob);

        // Arrays shrink as well as grow.
        Everything big = makeFilled();
        big.tags.resize(50, "x");
        Everything empty;
        CHECK(reflect::readBinary(big, reflect::toBlob(empty)));
        CHECK(big.tags.empty() && big.route.empty() && big.grid.empty());
    }

    void testBinaryExactness()
    {
        section("binary exactness");

        Everything e;
        e.f = -0.0f;
        e.d = std::numeric_limits<double>::quiet_NaN();
        e.v3 = { std::numeric_limits<float>::infinity(), -0.0f, std::numeric_limits<float>::denorm_min() };

        Everything out;
        CHECK(reflect::readBinary(out, reflect::toBlob(e)));
        CHECK(sameBits(out.f, -0.0f));
        CHECK(std::isnan(out.d));
        CHECK(std::isinf(out.v3.x));
        CHECK(sameBits(out.v3.y, -0.0f));
        CHECK(out.v3.z == std::numeric_limits<float>::denorm_min());

        // Enum value with no registered name still round-trips.
        e.mood = static_cast<Mood>(-32000);
        CHECK(reflect::readBinary(out, reflect::toBlob(e)));
        CHECK(out.mood == static_cast<Mood>(-32000));
    }

    void testBinaryRejection()
    {
        section("binary rejection");

        const Everything source = makeFilled();
        const reflect::Blob blob = reflect::toBlob(source);
        const Everything pristine;

        // Every truncation fails and leaves the target untouched.
        bool allFailed = true;
        bool allUntouched = true;
        for (size_t cut = 0; cut < blob.size(); ++cut) {
            Everything target;
            const bool ok = reflect::readBinary(target, std::span<const uint8_t>(blob.data(), cut));
            allFailed    &= !ok;
            allUntouched &= equalEverything(target, pristine) && target.home.restoredCount == 0;
        }
        CHECK(allFailed);
        CHECK(allUntouched);

        // Trailing bytes are rejected by readBinary, accepted by the prefix form.
        reflect::Blob longer = blob;
        longer.push_back(0);
        Everything target;
        CHECK(!reflect::readBinary(target, longer));
        CHECK(equalEverything(target, pristine));
        size_t consumed = 0;
        CHECK(reflect::readBinaryPrefix(reflect::typeOf<Everything>(), &target, longer, consumed));
        CHECK(consumed == blob.size());

        // Wrong type: the header catches it.
        const reflect::Blob waypointBlob = reflect::toBlob(Waypoint{});
        Everything wrong;
        CHECK(!reflect::readBinary(wrong, waypointBlob));

        // Random corruption never crashes, and a failure never partially applies.
        std::mt19937 rng(1234);
        bool neverPartial = true;
        for (int round = 0; round < 3000; ++round) {
            reflect::Blob bad = blob;
            const int flips = 1 + static_cast<int>(rng() % 4);
            for (int k = 0; k < flips; ++k) {
                bad[rng() % bad.size()] ^= static_cast<uint8_t>(1u << (rng() % 8));
            }
            Everything victim;
            if (!reflect::readBinary(victim, bad)) {
                neverPartial &= equalEverything(victim, pristine);
            }
        }
        CHECK(neverPartial);

        // A huge array count in a small blob is rejected before any resize.
        reflect::Blob hostile = reflect::toBlob(Everything{});
        // The route count sits right after "home"; find it by rewriting every
        // zero u32 in turn and making sure nothing explodes.
        for (size_t at = 8; at + 4 <= hostile.size(); ++at) {
            reflect::Blob probe = hostile;
            const uint32_t huge = 0xFFFFFFF0u;
            std::memcpy(probe.data() + at, &huge, 4);
            Everything victim;
            (void)reflect::readBinary(victim, probe);
        }
        CHECK(true);
    }

    void testJsonRoundTrip()
    {
        section("json round trip");

        const Everything source = makeFilled();
        const reflect::Json json = reflect::toJson(source);

        CHECK(json["f"].dump() == "0.3");                  // shortest float text
        CHECK(json["v3"].dump() == "[0.1,0.2,0.3]");
        CHECK(json["mood"] == "Sleepy");
        CHECK(json["id"] == "0123456789abcdef");
        CHECK(json["target"] == "000000000000002a");
        CHECK(json["home"]["$version"] == 2);              // nested type is v2
        CHECK(!json.contains("$version"));                 // top level: caller's job
        CHECK(!json.contains("cachedLength"));
        CHECK(!json.contains("gpuHandle"));             // runtime handle: never in files
        CHECK(json["q"].size() == 4);
        CHECK(json.begin().key() == "flag");               // declaration order

        // Through text and back.
        const reflect::Json reparsed = reflect::Json::parse(json.dump(2));
        Everything target;
        target.gpuHandle = source.gpuHandle;            // files cannot carry it
        std::vector<std::string> warnings;
        CHECK(reflect::fromJson(target, reparsed, 1, &warnings));
        CHECK(warnings.empty());
        CHECK(equalEverything(source, target));
        CHECK(reflect::toJson(target).dump() == json.dump());   // stable text
        CHECK(reflect::toBlob(target) == reflect::toBlob(source));

        Everything fresh;
        CHECK(reflect::fromJson(fresh, reparsed, 1));
        CHECK(fresh.gpuHandle == 0);

        // Null NodeRef/Guid are JSON null.
        const reflect::Json empty = reflect::toJson(Everything{});
        CHECK(empty["id"].is_null());
        CHECK(empty["target"].is_null());

        // Every float bit pattern class survives text.
        std::mt19937 rng(99);
        bool floatsExact = true;
        for (int i = 0; i < 20000; ++i) {
            const auto bits = static_cast<uint32_t>(rng());
            float value = 0.0f;
            std::memcpy(&value, &bits, 4);
            if (std::isnan(value)) continue;
            Everything e;
            e.f = value;
            Everything back;
            const auto text = reflect::Json::parse(reflect::toJson(e).dump());
            (void)reflect::fromJson(back, text, 1);
            floatsExact &= sameBits(back.f, value);
        }
        CHECK(floatsExact);

        Everything special;
        special.f = -std::numeric_limits<float>::infinity();
        special.d = std::numeric_limits<double>::quiet_NaN();
        special.v2 = { -0.0f, std::numeric_limits<float>::infinity() };
        const reflect::Json sj = reflect::Json::parse(reflect::toJson(special).dump());
        CHECK(sj["f"] == "-inf");
        CHECK(sj["d"] == "nan");
        Everything specialBack;
        CHECK(reflect::fromJson(specialBack, sj, 1));
        CHECK(std::isinf(specialBack.f) && specialBack.f < 0);
        CHECK(std::isnan(specialBack.d));
        CHECK(sameBits(specialBack.v2.x, -0.0f));
    }

    void testJsonTolerance()
    {
        section("json tolerance");

        const reflect::Json data = reflect::Json::parse(R"({
            "i32": "not a number",
            "u32": -1,
            "i64": 3.0,
            "f": 2,
            "v3": [1, 2],
            "q": [0, 0, 0],
            "mood": "Ecstatic",
            "id": "XYZ",
            "text": 5,
            "flag": 1,
            "tags": "enemy",
            "route": [ {"wait": 3}, {"wait": "long"} ],
            "home": { "wait": 9, "surprise": true },
            "removedField": 1,
            "cachedLength": 4,
            "gpuHandle": 99,
            "$comment": "metadata keys are ignored"
        })");

        Everything target = makeFilled();
        const Everything before = target;
        std::vector<std::string> warnings;
        CHECK(reflect::fromJson(target, data, 1, &warnings));

        // Bad values keep what was there.
        CHECK(target.i32 == before.i32);
        CHECK(target.u32 == before.u32);
        CHECK(target.i64 == before.i64);            // floats not accepted for 64-bit
        CHECK(target.v3 == before.v3);
        CHECK(target.q.w == before.q.w);
        CHECK(target.mood == before.mood);
        CHECK(target.id == before.id);
        CHECK(target.text == before.text);
        CHECK(target.flag == before.flag);
        CHECK(target.tags == before.tags);
        CHECK(target.cachedLength == before.cachedLength);
        CHECK(target.gpuHandle == before.gpuHandle);

        // Good values apply.
        CHECK(target.f == 2.0f);
        CHECK(target.home.wait == 9.0f);
        CHECK(target.home.position == before.home.position);   // missing -> kept
        CHECK(target.route.size() == 2);
        CHECK(target.route[0].wait == 3.0f);

        // Untouched fields stay.
        CHECK(target.grid == before.grid);

        CHECK(hasWarning(warnings, "Everything.i32:"));
        CHECK(hasWarning(warnings, "Everything.u32:"));
        CHECK(hasWarning(warnings, "Everything.i64:"));
        CHECK(hasWarning(warnings, "Everything.v3:"));
        CHECK(hasWarning(warnings, "Everything.q:"));
        CHECK(hasWarning(warnings, "unknown Mood value 'Ecstatic'"));
        CHECK(hasWarning(warnings, "Everything.id:"));
        CHECK(hasWarning(warnings, "Everything.text:"));
        CHECK(hasWarning(warnings, "Everything.flag:"));
        CHECK(hasWarning(warnings, "Everything.tags:"));
        CHECK(hasWarning(warnings, "Everything.route[1].wait:"));
        CHECK(hasWarning(warnings, "Everything.home: unknown field 'surprise'"));
        CHECK(hasWarning(warnings, "unknown field 'removedField'"));
        CHECK(hasWarning(warnings, "unknown field 'cachedLength'"));
        CHECK(hasWarning(warnings, "unknown field 'gpuHandle'"));
        CHECK(!hasWarning(warnings, "$comment"));

        // 32-bit fields accept integral floats; enums accept raw integers.
        const reflect::Json lenient = reflect::Json::parse(R"({"i32": 3.0, "u32": 7.5, "mood": 7})");
        Everything l;
        warnings.clear();
        CHECK(reflect::fromJson(l, lenient, 1, &warnings));
        CHECK(l.i32 == 3);
        CHECK(l.u32 == 0);
        CHECK(l.mood == Mood::Angry);
        CHECK(warnings.size() == 1);

        // Hard failures leave the object untouched.
        Everything untouched = makeFilled();
        CHECK(!reflect::fromJson(untouched, reflect::Json::array(), 1));
        CHECK(!reflect::fromJson(untouched, reflect::Json::object(), 2));   // newer than code
        CHECK(!reflect::fromJson(untouched, reflect::Json::object(), 0));
        CHECK(equalEverything(untouched, makeFilled()));

        // Nested struct from a newer version is skipped with a warning.
        const reflect::Json newerNested = reflect::Json::parse(R"({"home": {"$version": 3, "wait": 1}})");
        Everything n = makeFilled();
        warnings.clear();
        CHECK(reflect::fromJson(n, newerNested, 1, &warnings));
        CHECK(n.home.wait == makeFilled().home.wait);
        CHECK(hasWarning(warnings, "Everything.home: data has version 3"));

        // Nested struct without $version is treated as v1 and read normally.
        const reflect::Json olderNested = reflect::Json::parse(R"({"home": {"wait": 1}})");
        warnings.clear();
        CHECK(reflect::fromJson(n, olderNested, 1, &warnings));
        CHECK(n.home.wait == 1.0f);
        CHECK(warnings.empty());
    }

    void testJsonMigration()
    {
        section("json migration");

        const reflect::Json v1 = reflect::Json::parse(R"({"speed": 12.5})");
        Versioned out;
        std::vector<std::string> warnings;
        CHECK(reflect::fromJson(out, v1, 1, &warnings));
        CHECK(out.velocity == 12.5f);
        CHECK(out.extra == 5.0f);        // added field keeps its default
        CHECK(warnings.empty());         // "speed" was migrated away

        // Current-version data is not run through migrate().
        const reflect::Json v2 = reflect::Json::parse(R"({"velocity": 3, "speed": 99})");
        Versioned current;
        warnings.clear();
        CHECK(reflect::fromJson(current, v2, 2, &warnings));
        CHECK(current.velocity == 3.0f);
        CHECK(hasWarning(warnings, "unknown field 'speed'"));
    }

    void testNodeReflection()
    {
        section("node reflection");

        NodeWorld world;
        world.initialize(8);
        auto [a, aId] = world.createNode();
        a.name = "Sun";
        a.setTranslation({ 1.0f, 2.0f, 3.0f });
        a.setRotation(glm::normalize(glm::quat(0.7f, 0.1f, 0.6f, 0.2f)));
        a.setScale({ 2.0f, 2.0f, 2.0f });
        a.lightType = LightType::Directional;
        a.lightColor = { 1.0f, 0.5f, 0.25f };
        a.lightIntensity = 7.0f;
        a.lightCastsShadows = false;
        a.meshId = 5;

        const reflect::Blob blob = reflect::toBlob(world.getNode(aId));

        auto [b, bId] = world.createNode();
        const Guid bGuid = b.guid();
        (void)b.getTransform();                  // clear the local dirty flag
        b.clearChanged();
        CHECK(!world.getNode(bId).hasChanged());

        CHECK(reflect::readBinary(world.getNode(bId), blob));
        Node &copy = world.getNode(bId);
        CHECK(copy.name == "Sun");
        CHECK(copy.getTranslation() == world.getNode(aId).getTranslation());
        CHECK(copy.getScale() == world.getNode(aId).getScale());
        CHECK(copy.lightType == LightType::Directional);
        CHECK(copy.lightIntensity == 7.0f);
        CHECK(!copy.lightCastsShadows);
        CHECK(copy.hasChanged());                           // afterRead ran
        CHECK(copy.getTransform() == world.getNode(aId).getTransform());   // cache rebuilt
        CHECK(copy.guid() == bGuid);                        // identity untouched
        CHECK(copy.meshId == 5);                            // runtime handle: in snapshots
        CHECK(world.findNode(bGuid) == bId);

        const reflect::Json json = reflect::toJson(world.getNode(aId));
        CHECK(json["lightType"] == "Directional");
        CHECK(!json.contains("meshId"));
        CHECK(!json.contains("guid"));
        CHECK(!json.contains("parentId"));

        auto [c, cId] = world.createNode();
        (void)c;
        CHECK(reflect::fromJson(world.getNode(cId), reflect::Json::parse(json.dump()), 1));
        CHECK(world.getNode(cId).meshId == 0);              // ...but never in files
        world.getNode(cId).meshId = 5;
        CHECK(reflect::toBlob(world.getNode(cId)) == blob);
    }

    void testNodeIdentity()
    {
        section("node identity");

        NodeWorld world;
        world.initialize(16);

        auto [n1, id1] = world.createNode();
        const Guid g1 = n1.guid();
        CHECK(!g1.isNull());
        CHECK(world.findNode(g1) == id1);
        CHECK(world.findNode(Guid{}) == 0);
        CHECK(world.findNode(Guid::generate()) == 0);

        auto [n2, id2] = world.createNode();
        const Guid g2 = n2.guid();
        CHECK(g2 != g1);

        // Delete -> the Guid is gone, the slot is recycled under a NEW Guid.
        world.markDead(id1);
        CHECK(world.findNode(g1) == 0);
        CHECK(world.findNode(g2) == id2);

        auto [n3, id3] = world.createNode();
        CHECK(id3 == id1);                  // same slot...
        CHECK(n3.guid() != g1);             // ...different object
        CHECK(world.findNode(n3.guid()) == id3);
        CHECK(world.findNode(g1) == 0);

        // Resurrect g1 (what undo of the delete will do): new slot, old identity.
        auto [back, backId] = world.createNode(g1);
        CHECK(back.guid() == g1);
        CHECK(backId != id1);
        CHECK(world.findNode(g1) == backId);

        // Double-kill is harmless and does not disturb the map.
        world.markDead(backId);
        world.markDead(backId);
        CHECK(world.findNode(g1) == 0);
        CHECK(world.findNode(g2) == id2);

        // Churn: the map always matches the alive set exactly.
        std::mt19937 rng(7);
        std::vector<std::pair<Guid, uint32_t>> live{ { g2, id2 }, { world.getNode(id3).guid(), id3 } };
        std::vector<Guid> dead;
        bool consistent = true;
        for (int step = 0; step < 5000; ++step) {
            const bool create = live.size() < 2 || (live.size() < 16 && (rng() & 1));
            if (create) {
                Guid wanted{};
                if (!dead.empty() && (rng() % 3 == 0)) {
                    const size_t k = rng() % dead.size();
                    wanted = dead[k];
                    dead.erase(dead.begin() + static_cast<long>(k));
                }
                auto [node, id] = world.createNode(wanted);
                consistent &= wanted.isNull() || node.guid() == wanted;
                live.emplace_back(node.guid(), id);
            } else {
                const size_t k = rng() % live.size();
                world.markDead(live[k].second);
                dead.push_back(live[k].first);
                live.erase(live.begin() + static_cast<long>(k));
            }
            for (const auto &[guid, id] : live) {
                consistent &= world.findNode(guid) == id && world.isAlive(id) &&
                              world.getNode(id).guid() == guid;
            }
            for (const Guid guid : dead) {
                consistent &= world.findNode(guid) == 0;
            }
            consistent &= world.liveCount() == live.size();
        }
        CHECK(consistent);
    }

    void testMaterialReflection()
    {
        section("material reflection");

        Material m;
        m.name             = "Brushed Metal";
        m.baseColorFactor  = { 0.8f, 0.8f, 0.82f, 1.0f };
        m.metallicFactor   = 1.0f;
        m.roughnessFactor  = 0.35f;
        m.alphaMode        = AlphaMode::Mask;
        m.doubleSided      = true;
        m.baseColorTexture = 12;
        m.normalTexture    = 3;

        Material copy;
        CHECK(reflect::readBinary(copy, reflect::toBlob(m)));
        CHECK(copy.name == m.name && copy.roughnessFactor == 0.35f);
        CHECK(copy.alphaMode == AlphaMode::Mask && copy.doubleSided);
        CHECK(copy.baseColorTexture == 12 && copy.normalTexture == 3);   // undo keeps handles

        const reflect::Json json = reflect::toJson(m);
        CHECK(json["alphaMode"] == "Mask");
        CHECK(json["roughnessFactor"].dump() == "0.35");
        CHECK(!json.contains("baseColorTexture"));                         // files do not

        // Same keys as the .mat format, so the eventual switch is free.
        for (const char *key : { "name", "baseColorFactor", "metallicFactor", "roughnessFactor",
                                 "emissiveFactor", "emissiveStrength", "normalScale",
                                 "occlusionStrength", "alphaMode", "alphaCutoff", "doubleSided" }) {
            CHECK(json.contains(key));
        }
    }

    // Runs last: it deliberately registers a type with a hole in it.
    void testValidate()
    {
        section("validate");

        CHECK(reflect::validate().empty());

        reflect::registerType<HasUnregistered>("HasUnregistered", 1)
            .field<&HasUnregistered::inner>("inner");
        const auto errors = reflect::validate();
        CHECK(errors.size() == 1);
        CHECK(!errors.empty() && errors[0].find("HasUnregistered.inner") != std::string::npos);
    }
}

void runReflectionTests()
{
    registerTestTypes();

    testGuid();
    testRegistry();
    testBinaryRoundTrip();
    testBinaryExactness();
    testBinaryRejection();
    testJsonRoundTrip();
    testJsonTolerance();
    testJsonMigration();
    testNodeReflection();
    testMaterialReflection();
    testNodeIdentity();
}

// Runs last of all: it deliberately registers a type with a hole in it.
void runValidateTest()
{
    testValidate();
}

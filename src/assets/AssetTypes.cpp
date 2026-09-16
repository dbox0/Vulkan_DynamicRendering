#include "AssetTypes.h"

#include "Material.h"
#include "Mesh.h"
#include "../reflect/Reflection.h"

// MATERIAL
//
// The five texture slots are 1-based ResourceStore IDs
// They are RuntimeHandle: undo restores them
//
// The .mat format is still written by MaterialSerializer, which turns those
// IDs into paths. Once texture references are asset references rather than
// IDs, that serializer collapses into JsonArchive and this registration is
// the only description of a material left.

void registerAssetTypes()
{
    reflect::registerEnum<AlphaMode>("AlphaMode")
        .value("Opaque", AlphaMode::Opaque)
        .value("Mask", AlphaMode::Mask)
        .value("Blend", AlphaMode::Blend);

    constexpr uint32_t handle = reflect::FieldFlags::RuntimeHandle;

    reflect::registerType<Material>("Material", 1)
        .field<&Material::name>("name")
        .field<&Material::baseColorFactor>("baseColorFactor").hint(reflect::FieldHint::Color)
        .field<&Material::metallicFactor>("metallicFactor").range(0.0f, 1.0f)
        .field<&Material::roughnessFactor>("roughnessFactor").range(0.0f, 1.0f)
        .field<&Material::emissiveFactor>("emissiveFactor").hint(reflect::FieldHint::Color)
        .field<&Material::emissiveStrength>("emissiveStrength")
        .field<&Material::normalScale>("normalScale")
        .field<&Material::occlusionStrength>("occlusionStrength").range(0.0f, 1.0f)
        .field<&Material::alphaMode>("alphaMode")
        .field<&Material::alphaCutoff>("alphaCutoff").range(0.0f, 1.0f)
        .field<&Material::doubleSided>("doubleSided")
        .field<&Material::baseColorTexture>("baseColorTexture", handle)
        .field<&Material::metallicRoughnessTexture>("metallicRoughnessTexture", handle)
        .field<&Material::normalTexture>("normalTexture", handle)
        .field<&Material::occlusionTexture>("occlusionTexture", handle)
        .field<&Material::emissiveTexture>("emissiveTexture", handle);

    reflect::registerType<MeshMaterials>("MeshMaterials", 1)
        .field<&MeshMaterials::materials>("materials", handle);
}

#include <iostream>

int main()
{
    // POC C models the future native registration/code-generation output. The
    // descriptor is emitted without loading the component implementation into
    // an editor process.
constexpr auto manifest = R"json({
  "$schema": "https://dragonpixel.dev/schemas/v2/component-metadata.schema.json",
  "format": "dpe.component-metadata",
  "formatVersion": 2,
  "generatorVersion": "1",
  "components": [
    {
      "typeId": "ef688879-c3ca-4711-8ed4-2fb5e0b3dfc5",
      "qualifiedName": "DragonPixel.PocC.NativeTransformComponent",
      "displayName": "Transform",
      "schemaVersion": 1,
      "owner": "native",
      "properties": [
        {
          "propertyId": "dpe.poc.transform.translation",
          "displayName": "Translation",
          "valueType": "vector3",
          "order": 0,
          "readOnly": false
        },
        {
          "propertyId": "dpe.poc.transform.rotation",
          "displayName": "Rotation",
          "valueType": "quaternion",
          "order": 1,
          "readOnly": false
        },
        {
          "propertyId": "dpe.poc.transform.visible",
          "displayName": "Visible",
          "valueType": "boolean",
          "order": 2,
          "readOnly": false
        }
      ]
    }
  ]
})json";

    std::cout << manifest << '\n';
    return std::cout.good() ? 0 : 1;
}

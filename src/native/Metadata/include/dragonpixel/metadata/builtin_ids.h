#pragma once

#include <string_view>

namespace dragonpixel::metadata::builtin_component_ids
{
inline constexpr std::string_view transform = "52e52fbd-ea15-40c5-bd9a-7dd320f7cd1e";
inline constexpr std::string_view rotator = "ee40709b-2bfc-4d50-b729-8612fb60d478";
inline constexpr std::string_view camera = "4d054cdc-20e0-4f4f-80d9-a38923c36d46";
inline constexpr std::string_view sprite = "b527395a-93a5-44f3-8d6c-7ea83a8568d1";
inline constexpr std::string_view mesh = "9be44558-78e9-4912-bee5-046b5ad0a410";
inline constexpr std::string_view material = "90d93631-746a-4f52-9f95-4895e27edf51";
inline constexpr std::string_view light = "1859c426-7cfe-42fc-a815-f5fc1bf1dad6";
inline constexpr std::string_view rigid_body_2d = "ea23c9e1-2580-4aa9-94a2-96de0c5545ac";
inline constexpr std::string_view box_collider_2d = "edbe79a3-4e97-4440-a23d-05c2d8faa1ca";
inline constexpr std::string_view circle_collider_2d = "aee65743-286c-431e-8aaf-321cac47eaa1";
inline constexpr std::string_view rigid_body_3d = "1025c21c-34a8-4f64-977b-453d81e402c5";
inline constexpr std::string_view box_collider_3d = "bfc9ff93-a892-4c1f-ad8f-f13d8bc1ba38";
inline constexpr std::string_view sphere_collider_3d = "11f84a3a-b568-4107-ad02-c53a86e50971";
inline constexpr std::string_view tilemap_2d = "eea820b4-79e4-4dd6-86f8-04c93c3486fb";
inline constexpr std::string_view tilemap_collider_2d = "43d833f1-6ff4-4fd1-9a62-90d230118e7b";
inline constexpr std::string_view input_motion_2d = "64348aba-c5a4-42fc-86e6-e99f9640e36d";
}

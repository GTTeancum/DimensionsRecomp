#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
namespace legodimensions::native_roster {
// Replace/extend with exact IDs decoded from integrity-verified companion
// resources. One row is one independently authored three-build vehicle family.
// Verified local resource receipt SHA256: 1192df191d8c6e1876392e150316ce30f26aa432a8ea507e472cbcc101c7559c
inline constexpr std::array<std::array<uint32_t, 3>, 80> kVehicleFamilies{{
  {{1170, 1171, 1172}}, // Adventure Time / Ancient Psychic Tandem War Elephant
  {{1176, 1177, 1178}}, // Adventure Time / Jakemobile
  {{1173, 1174, 1175}}, // Adventure Time / BMO
  {{1179, 1180, 1181}}, // Adventure Time / Lumpy Car
  {{1182, 1183, 1184}}, // Adventure Time / Lunatic Amp
  {{1099, 1100, 1101}}, // Back to the Future / Travelling Time Train
  {{1012, 1013, 1014}}, // Back to the Future / DeLorean Time Machine
  {{1015, 1016, 1017}}, // Back to the Future / Hoverboard
  {{1245, 1246, 1247}}, // Beetlejuice / Saturn's Sandworm
  {{1102, 1103, 1104}}, // DC Comics / Aqua Watercraft
  {{1105, 1106, 1107}}, // DC Comics / Drill Driver
  {{1006, 1007, 1008}}, // DC Comics / Batmobile
  {{1024, 1025, 1026}}, // DC Comics / Cyber-Guard
  {{1108, 1109, 1110}}, // DC Comics / Quinn-mobile
  {{1114, 1115, 1115}}, // DC Comics / Hover Pod
  {{1111, 1112, 1113}}, // DC Comics / The Joker's Chopper
  {{1093, 1094, 1095}}, // DC Comics / Invisible Jet
  {{1117, 1118, 1119}}, // Doctor Who / Dalek
  {{1027, 1028, 1029}}, // Doctor Who / K-9
  {{1030, 1031, 1032}}, // Doctor Who / TARDIS
  {{1188, 1189, 1190}}, // E.T. the Extra-Terrestrial / Phone Home
  {{1191, 1192, 1193}}, // Fantastic Beasts and Where to Find Them / Niffler
  {{1194, 1195, 1196}}, // Fantastic Beasts and Where to Find Them / Swooping Evil
  {{1120, 1121, 1122}}, // Ghostbusters / Ecto-1
  {{1123, 1124, 1125}}, // Ghostbusters / Ghost Trap
  {{1164, 1165, 1166}}, // Ghostbusters / Slime Shooter
  {{1167, 1168, 1169}}, // Ghostbusters / Terror Dog
  {{1197, 1198, 1199}}, // Ghostbusters 2016 / Ecto-1 (2016)
  {{1203, 1204, 1205}}, // Gremlins / R.C. Racer
  {{1200, 1201, 1202}}, // Gremlins / Flash 'n' Finish
  {{1209, 1210, 1211}}, // Harry Potter / Enchanted Car
  {{1242, 1243, 1244}}, // Harry Potter / Buckbeak
  {{1206, 1207, 1208}}, // Harry Potter / Hogwarts Express
  {{1054, 1055, 1056}}, // Jurassic World / Gyrosphere
  {{1051, 1052, 1053}}, // Jurassic World / Velociraptor
  {{1224, 1225, 1226}}, // Knight Rider / K.I.T.T.
  {{1021, 1022, 1023}}, // Legends of Chima / Swamp Skimmer
  {{1018, 1019, 1020}}, // Legends of Chima / Eagle Interceptor
  {{1060, 1061, 1062}}, // Legends of Chima / Mighty Lion Rider
  {{1227, 1228, 1229}}, // LEGO City Undercover / Police Helicopter
  {{1158, 1159, 1160}}, // Midway Arcade / Arcade Machine
  {{1161, 1162, 1163}}, // Midway Arcade / G-6155 Spy Hunter
  {{1212, 1213, 1214}}, // Mission Impossible / IMF Scrambler
  {{1215, 1216, 1217}}, // Mission Impossible / IMF Sport Car
  {{1069, 1070, 1071}}, // Ninjago / Boulder Bomber
  {{1072, 1073, 1074}}, // Ninjago / Storm Fighter
  {{1075, 1076, 1077}}, // Ninjago / Blade Bike
  {{1132, 1133, 1144}}, // Ninjago / Lloyd's Golden Dragon
  {{1078, 1079, 1080}}, // Ninjago / Samurai Mech
  {{1155, 1156, 1157}}, // Ninjago / Flying White Dragon
  {{1096, 1097, 1098}}, // Ninjago / NinjaCopter
  {{1081, 1082, 1083}}, // Portal 2 / Companion Cube
  {{1084, 1085, 1086}}, // Portal 2 / Sentry Turret
  {{1087, 1088, 1089}}, // Scooby-Doo! / Scooby Snack
  {{1066, 1067, 1068}}, // Scooby-Doo! / Mystery Machine
  {{1218, 1219, 1220}}, // Sonic the Hedgehog / Sonic Speedster
  {{1221, 1222, 1223}}, // Sonic the Hedgehog / The Tornado
  {{1257, 1258, 1259}}, // Teen Titans Go! / T-Car
  {{1260, 1261, 1262}}, // Teen Titans Go! / Spellbook of Azarath
  {{1263, 1264, 1265}}, // Teen Titans Go! / Titan Robot
  {{1185, 1186, 1187}}, // The A-Team / B. A. 's Van
  {{1239, 1240, 1241}}, // The Goonies / One-Eyed Willy's Pirate Ship
  {{1236, 1237, 1238}}, // The Goonies / Skeleton Organ
  {{1233, 1234, 1235}}, // The LEGO Batman Movie / Batwing
  {{1230, 1231, 1232}}, // The LEGO Batman Movie / Bionic Steed
  {{1000, 1001, 1002}}, // The LEGO Movie / Police Car
  {{1009, 1010, 1011}}, // The LEGO Movie / Benny's Spaceship
  {{1033, 1034, 1035}}, // The LEGO Movie / Emmet's Excavator
  {{1090, 1091, 1092}}, // The LEGO Movie / Cloud Cuckoo Car
  {{1039, 1040, 1041}}, // The Lord of the Rings / Axe Chariot
  {{1042, 1043, 1044}}, // The Lord of the Rings / Shelob the Great
  {{1063, 1064, 1065}}, // The Lord of the Rings / Arrow Launcher
  {{1248, 1249, 1250}}, // The Powerpuff Girls / PPG Smartphone
  {{1254, 1255, 1256}}, // The Powerpuff Girls / Octi
  {{1253, 1252, 1251}}, // The Powerpuff Girls / Mega Blast Bot
  {{1003, 1004, 1005}}, // The Simpsons / Gravity Sprinter
  {{1045, 1046, 1047}}, // The Simpsons / Homer's Car
  {{1048, 1049, 1050}}, // The Simpsons / Taunt-o-Vision
  {{1057, 1058, 1059}}, // The Simpsons / Clown Bike
  {{1036, 1037, 1038}}, // The Wizard of Oz / Winged Monkey
}};
inline int VehicleFamily(uint32_t id) {
  for (size_t i = 0; i < kVehicleFamilies.size(); ++i)
    for (auto member : kVehicleFamilies[i]) if (member == id) return static_cast<int>(i);
  return -1;
}
inline bool KnownVehicle(uint32_t id) { return VehicleFamily(id) >= 0; }
inline bool PermittedModel(uint32_t initial, uint32_t current) {
  if (initial == current) return true;
  if (initial == 0) return KnownVehicle(current); // Existing blank initialized by game.
  const int family = VehicleFamily(initial);
  return family >= 0 && VehicleFamily(current) == family;
}
} // namespace legodimensions::native_roster

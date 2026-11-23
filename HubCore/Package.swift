// swift-tools-version:5.9
// HubCore — la logique pure de SDR Driver Hub (protocole rtl_tcp, contrôle du
// contenu IQ, file d'envoi), sans DriverKit ni UIKit : testée sur le Mac par
// `swift test`, compilée telle quelle dans l'app iPad.
import PackageDescription

let package = Package(
    name: "HubCore",
    platforms: [.macOS(.v13), .iOS(.v16)],
    products: [.library(name: "HubCore", targets: ["HubCore"])],
    targets: [
        // Le paquet sert aux tests sur le Mac, qui couvrent aussi le protocole
        // rtl_tcp de développement. L'app iPad compile ces sources avec ses
        // propres conditions : sans HUB_DEV_RTLTCP dans un build App Store.
        .target(name: "HubCore", swiftSettings: [.define("HUB_DEV_RTLTCP")]),
        .testTarget(name: "HubCoreTests", dependencies: ["HubCore"]),
    ]
)

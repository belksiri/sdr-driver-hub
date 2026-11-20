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
        .target(name: "HubCore"),
        .testTarget(name: "HubCoreTests", dependencies: ["HubCore"]),
    ]
)

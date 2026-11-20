//
//  ProbeMetrics.swift — CPU, mémoire, thermique, batterie.
//
//  CPU et mémoire s'obtiennent par les mêmes appels Mach sur les deux
//  plateformes. `thermalState` existe des deux côtés. La batterie, non : sur Mac
//  elle n'a pas le même sens, et la valeur -1 signale honnêtement « non
//  applicable » plutôt que d'inventer 100 %.
//
//  Ces mesures sont l'un des points où le Mac ne prédit RIEN de l'iPad : 10
//  cœurs et 16 Go sous macOS ne disent pas ce que feront 8 cœurs sous iPadOS,
//  avec un ordonnanceur différent et une enveloppe thermique de tablette.
//

import Foundation
#if canImport(UIKit)
import UIKit
#endif

struct ProbeMetricsSample: Sendable {
    var cpuPercent: Double = 0
    var memoryMB: Double = 0
    var thermalState: String = "nominal"
    var batteryPercent: Double = -1
}

enum ProbeMetrics {

    /// Charge CPU du processus, en pourcentage d'un cœur. Somme des threads.
    static func processCPUPercent() -> Double {
        var threadList: thread_act_array_t?
        var threadCount: mach_msg_type_number_t = 0
        guard task_threads(mach_task_self_, &threadList, &threadCount) == KERN_SUCCESS,
              let threads = threadList else { return 0 }
        defer {
            vm_deallocate(mach_task_self_, vm_address_t(UInt(bitPattern: threads)),
                          vm_size_t(Int(threadCount) * MemoryLayout<thread_t>.size))
        }

        var total = 0.0
        for i in 0..<Int(threadCount) {
            var info = thread_basic_info()
            // THREAD_BASIC_INFO_COUNT est une macro C non exposée à Swift : la
            // taille se calcule, elle ne se devine pas.
            var count = mach_msg_type_number_t(
                MemoryLayout<thread_basic_info_data_t>.size / MemoryLayout<natural_t>.size)
            let result = withUnsafeMutablePointer(to: &info) {
                $0.withMemoryRebound(to: integer_t.self, capacity: Int(count)) {
                    thread_info(threads[i], thread_flavor_t(THREAD_BASIC_INFO), $0, &count)
                }
            }
            guard result == KERN_SUCCESS else { continue }
            if info.flags & TH_FLAGS_IDLE == 0 {
                total += Double(info.cpu_usage) / Double(TH_USAGE_SCALE) * 100.0
            }
        }
        return total
    }

    /// Empreinte mémoire physique — la grandeur qu'iPadOS surveille réellement,
    /// et celle qui décide d'une expulsion. `resident_size` serait optimiste.
    static func memoryFootprintMB() -> Double {
        var info = task_vm_info_data_t()
        var count = mach_msg_type_number_t(MemoryLayout<task_vm_info_data_t>.size / MemoryLayout<integer_t>.size)
        let result = withUnsafeMutablePointer(to: &info) {
            $0.withMemoryRebound(to: integer_t.self, capacity: Int(count)) {
                task_info(mach_task_self_, task_flavor_t(TASK_VM_INFO), $0, &count)
            }
        }
        guard result == KERN_SUCCESS else { return 0 }
        return Double(info.phys_footprint) / (1024.0 * 1024.0)
    }

    static func thermalStateName() -> String {
        switch ProcessInfo.processInfo.thermalState {
        case .nominal:  return "nominal"
        case .fair:     return "fair"
        case .serious:  return "serious"
        case .critical: return "critical"
        @unknown default: return "inconnu"
        }
    }

    static func batteryPercent() -> Double {
        #if canImport(UIKit) && !targetEnvironment(macCatalyst)
        UIDevice.current.isBatteryMonitoringEnabled = true
        let level = UIDevice.current.batteryLevel
        // -1 = indisponible. Le propager tel quel plutôt que d'afficher 0 %,
        // qui serait lu comme « batterie vide ».
        return level < 0 ? -1 : Double(level) * 100.0
        #else
        return -1
        #endif
    }

    static func sample() -> ProbeMetricsSample {
        ProbeMetricsSample(cpuPercent: processCPUPercent(),
                           memoryMB: memoryFootprintMB(),
                           thermalState: thermalStateName(),
                           batteryPercent: batteryPercent())
    }

    /// Étiquette de plateforme utilisée dans les noms de fichiers CSV.
    static var platformTag: String {
        #if os(macOS)
        return "MAC"
        #elseif os(iOS)
        return "IPAD"
        #else
        return "AUTRE"
        #endif
    }
}

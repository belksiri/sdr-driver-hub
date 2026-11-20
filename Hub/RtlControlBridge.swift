//
//  RtlControlBridge.swift — Pont C → Swift de brtl_transport (SDR Driver Hub).
//
//  Repris tel quel du prototype RTL-SDR iPadOS de Belksiri Spectrum
//  (ios/Runner/RtlSdr/RtlSdrIosSession.swift, branche
//  experiment/ipados-rtlsdr-v3-gpl), validé sur l'iPad avec le RTL-SDR Blog V3.
//  librtlsdr appelle ce point d'entrée ; la requête part au user client du dext.
//

import Foundation

/// Pont C → Swift pour brtl_transport : librtlsdr appelle ce point d'entrée,
/// qui emballe la requête dans le protocole du user client.
///
/// Classement des échecs (quatre familles, jamais confondues) :
///   • `tunerProbeMisses` — pendant l'ouverture, librtlsdr sonde l'I²C de
///     chaque tuner connu (E4000 0xC8, FC0012/FC0013 0xC6, FC2580 0xAC…) ;
///     ceux qui sont absents ne répondent pas. Attendu : ce n'est PAS une
///     erreur du périphérique, seulement une trace ;
///   • `tunerCommandFailures` — une requête I²C au tuner DÉTECTÉ a échoué ;
///   • `usbErrors` — toute autre requête en échec, ou le transport lui-même ;
///   • `denied` — refusée par la liste blanche du pilote, avant le bus.
/// Une fois le tuner identifié, aucune requête I²C ne doit viser une autre
/// adresse : `foreignI2cRequests` les compte (attendu : 0).
final class RtlControlBridge {
    let transport: HackRFTransport
    private(set) var denied: UInt64 = 0
    private(set) var usbErrors: UInt64 = 0
    private(set) var tunerProbeMisses: UInt64 = 0
    private(set) var tunerCommandFailures: UInt64 = 0
    private(set) var foreignI2cRequests: UInt64 = 0
    private(set) var lastDenyReason: UInt32 = 0
    /// Les 8 derniers vrais échecs, pour le diagnostic (« OUT 0x40
    /// wValue=0x00c8 wIndex=0x0610 len=1 → status 5 io 0xe000404f »).
    private(set) var recentFailures: [String] = []
    /// Les dernières sondes sans réponse (trace « tuner probe miss »).
    private(set) var recentProbeMisses: [String] = []

    /// Adresse I²C du tuner identifié ; nil pendant l'ouverture (sondage).
    private(set) var tunerAddress: UInt8?
    private var probing = false
    private var pendingProbe: [(addr: UInt8, line: String)] = []

    /// Bloc I²C du RTL2832U (wIndex = bloc << 8).
    static let i2cBlock: UInt16 = 6

    init(transport: HackRFTransport) { self.transport = transport }

    /// Ouverture : les échecs I²C sont mis de côté jusqu'à l'identification.
    func beginProbe() {
        probing = true
        tunerAddress = nil
        pendingProbe.removeAll()
    }

    /// Fin de l'ouverture : chaque échec I²C mis de côté devient une sonde
    /// manquée (autre adresse) ou un vrai échec (adresse du tuner détecté).
    func finishProbe(tunerAddress addr: UInt8?) {
        probing = false
        tunerAddress = (addr ?? 0) == 0 ? nil : addr
        for p in pendingProbe {
            if let a = tunerAddress, p.addr == a {
                tunerCommandFailures += 1
                append(&recentFailures, p.line)
            } else {
                tunerProbeMisses += 1
                append(&recentProbeMisses, "tuner probe miss: " + p.line)
            }
        }
        pendingProbe.removeAll()
    }

    var countersDictionary: [String: Any] {
        ["denied": denied, "usbErrors": usbErrors,
         "tunerProbeMisses": tunerProbeMisses,
         "tunerCommandFailures": tunerCommandFailures,
         "foreignI2cRequests": foreignI2cRequests,
         "recentFailures": recentFailures,
         "recentProbeMisses": recentProbeMisses]
    }

    private func append(_ list: inout [String], _ line: String) {
        list.append(line)
        if list.count > 8 { list.removeFirst(list.count - 8) }
    }

    private static func describe(_ bm: UInt8, _ value: UInt16, _ index: UInt16, _ length: UInt16,
                                 _ status: Int32, _ io: Int32) -> String {
        String(format: "%@ 0x%02x wValue=0x%04x wIndex=0x%04x len=%u → status %d io 0x%08x",
               (bm & 0x80) != 0 ? "IN" : "OUT", bm, value, index, length,
               status, UInt32(bitPattern: io))
    }

    private static func isI2c(_ bm: UInt8, _ req: UInt8, _ index: UInt16) -> Bool {
        (bm & 0x60) == 0x40 && req == 0 && (index >> 8) == i2cBlock
    }

    /// Un échec du périphérique (pas un refus de politique), classé.
    private func noteDeviceFailure(_ bm: UInt8, _ req: UInt8, _ value: UInt16, _ index: UInt16,
                                   _ length: UInt16, _ status: Int32, _ io: Int32) {
        let line = Self.describe(bm, value, index, length, status, io)
        if Self.isI2c(bm, req, index) {
            let addr = UInt8(truncatingIfNeeded: value)
            if probing {
                pendingProbe.append((addr, line))
                return
            }
            if let a = tunerAddress, addr == a {
                tunerCommandFailures += 1
                append(&recentFailures, line)
                return
            }
        }
        usbErrors += 1
        append(&recentFailures, line)
    }

    func transfer(_ bm: UInt8, _ req: UInt8, _ value: UInt16, _ index: UInt16,
                  _ data: UnsafeMutablePointer<UInt8>?, _ length: UInt16) -> Int32 {
        if !probing, let a = tunerAddress, Self.isI2c(bm, req, index),
           UInt8(truncatingIfNeeded: value) != a {
            foreignI2cRequests += 1
        }
        var r = BHRFControlRequest()
        r.bmRequestType = bm
        r.bRequest = req
        r.wValue = value
        r.wIndex = index
        r.wLength = length
        let n = Int(min(length, UInt16(BHRF_CTRL_MAX_DATA)))
        if (bm & 0x80) == 0, n > 0, let data = data {
            withUnsafeMutableBytes(of: &r.data) { dst in
                dst.baseAddress!.copyMemory(from: data, byteCount: n)
            }
        }
        let resp: BHRFControlResponse
        do {
            resp = try transport.controlRequest(r)
        } catch {
            usbErrors += 1
            append(&recentFailures, Self.describe(bm, value, index, length, -1, 0))
            return transport.state == .terminated ? -4 /* NO_DEVICE */ : -1 /* IO */
        }
        switch resp.status {
        case Int32(kBHRFStatusOK.rawValue):
            let got = Int(resp.actualLength)
            if (bm & 0x80) != 0, got > 0, let data = data {
                var copy = resp
                withUnsafeBytes(of: &copy.data) { src in
                    data.update(from: src.bindMemory(to: UInt8.self).baseAddress!,
                                count: min(got, n))
                }
            }
            return Int32(got)
        case Int32(kBHRFStatusDenied.rawValue):
            denied += 1
            lastDenyReason = resp.denyReason
            append(&recentFailures, Self.describe(bm, value, index, length, resp.status, 0) + " (policy)")
            return -3 /* LIBUSB_ERROR_ACCESS : refusé par la politique du pilote */
        case Int32(kBHRFStatusTerminated.rawValue), Int32(kBHRFStatusNoDevice.rawValue):
            usbErrors += 1
            append(&recentFailures, Self.describe(bm, value, index, length, resp.status, resp.ioReturn))
            return -4 /* NO_DEVICE */
        default:
            noteDeviceFailure(bm, req, value, index, length, resp.status, resp.ioReturn)
            return resp.ioReturn == Int32(bitPattern: 0xE00002D6) ? -7 /* TIMEOUT */ : -1 /* IO */
        }
    }
}

let brtlControlEntry: @convention(c) (
    UnsafeMutableRawPointer?, UInt8, UInt8, UInt16, UInt16,
    UnsafeMutablePointer<UInt8>?, UInt16, UInt32) -> Int32 = { ctx, bm, req, value, index, data, length, _ in
    guard let ctx = ctx else { return -2 }
    let bridge = Unmanaged<RtlControlBridge>.fromOpaque(ctx).takeUnretainedValue()
    return bridge.transfer(bm, req, value, index, data, length)
}

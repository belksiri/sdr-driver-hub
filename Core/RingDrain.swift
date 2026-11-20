//
//  RingDrain.swift — Le drainage du ring, partagé par tous les transports.
//
//  Avant ce fichier, le drainage vivait DANS le transport IOKit : copie dans un
//  tampon de transit, puis rien. L'intégration dans Belksiri Spectrum lui donne
//  deux destinataires — l'affichage et l'enregistrement — et il fallait qu'un
//  seul code les serve, qu'il tourne sur le vrai ring mappé ou sur un ring
//  synthétique de test. Sinon l'enregistrement aurait été prouvé sur un chemin
//  qui n'est pas celui du matériel.
//
//  Le drain ne connaît pas ses destinataires : il livre des blocs VALIDÉS
//  (`RingReader.drainValidated`) et déclare ses pertes. Ce qu'on en fait —
//  afficher 0,5 % des échantillons, ou les écrire tous — se décide au-dessus.
//

import Foundation

/// Reçoit les blocs du ring. Appelé sur la file de drainage : chaque appel
/// doit rester court (une copie, pas d'E/S), sous peine de faire perdre au
/// drain la course contre le pilote.
protocol RingBlockConsumer: AnyObject {
    /// Un bloc copié et validé. `bytes` n'est valable que pendant l'appel.
    func ringDelivered(_ bytes: UnsafeRawBufferPointer, index: UInt64, captureNs: UInt64)

    /// Des blocs perdus AVANT le consommateur : dépassés par le pilote, réécrits
    /// pendant la copie, ou publiés vides après une erreur USB. `bytes` est
    /// une estimation (taille nominale d'un bloc) : le contenu perdu n'a par
    /// définition jamais été lu.
    func ringLost(blocks: UInt64, bytes: UInt64)
}

final class RingDrain {

    private let reader = RingReader()

    /// Tampon de transit, réutilisé. Chaque bloc est recopié hors du ring AVANT
    /// validation : c'est la copie qu'on valide, jamais le ring lui-même, qui
    /// peut changer sous nos yeux.
    private var staging: [UInt8]

    /// Transferts en vol côté pilote, bornés par la MÊME fonction C que lui.
    private(set) var inFlight: UInt32 = 0

    /// Taille nominale d'un bloc du ring attaché, pour estimer les octets perdus.
    private(set) var blockBytes: UInt32 = UInt32(BHRF_RING_BLOCK_BYTES)

    weak var consumer: RingBlockConsumer?

    private(set) var deliveredBlocks: UInt64 = 0
    private(set) var deliveredBytes: UInt64 = 0
    private(set) var lappedBlocks: UInt64 = 0
    private(set) var tornBlocks: UInt64 = 0
    private(set) var emptyBlocks: UInt64 = 0

    /// Toutes les pertes du consommateur, quelle qu'en soit la cause. C'est ce
    /// compteur, et non les seuls dépassements, qui alimente `overflowBlocks`.
    var lostBlocks: UInt64 { lappedBlocks &+ tornBlocks &+ emptyBlocks }

    init(stagingBytes: Int = Int(BHRF_RING_BLOCK_BYTES)) {
        staging = [UInt8](repeating: 0, count: stagingBytes)
    }

    var isAttached: Bool { reader.isAttached }

    /// - Parameter requestedInFlight: ce que l'application a demandé au pilote.
    ///   Le pilote le borne avec `bhrf_clamp_in_flight` ; on applique la même
    ///   borne pour connaître la fenêtre réellement sûre.
    func attach(base: UnsafeRawPointer, mappedBytes: Int, requestedInFlight: UInt32) throws {
        try reader.attach(base: base, mappedBytes: mappedBytes)
        let header = base.assumingMemoryBound(to: BHRFRingHeader.self).pointee
        inFlight = bhrf_clamp_in_flight(requestedInFlight, header.blockCount)
        blockBytes = header.blockBytes
        if staging.count < Int(header.blockBytes) {
            staging = [UInt8](repeating: 0, count: Int(header.blockBytes))
        }
        resetCounters()
    }

    func detach() {
        reader.detach()
    }

    func resetCounters() {
        deliveredBlocks = 0
        deliveredBytes = 0
        lappedBlocks = 0
        tornBlocks = 0
        emptyBlocks = 0
    }

    /// Un tour de drainage. Renvoie vrai si le pilote a déclaré l'appareil
    /// terminé. À appeler depuis UNE seule file.
    @discardableResult
    func drainOnce(maxBlocks: Int = 64) -> Bool {
        guard reader.isAttached else { return false }
        var copied = 0
        let result = reader.drainValidated(
            maxBlocks: maxBlocks,
            inFlight: inFlight,
            copy: { [self] block in
                let n = min(block.count, staging.count)
                staging.withUnsafeMutableBytes { dst in
                    dst.baseAddress!.copyMemory(from: block.baseAddress!, byteCount: n)
                }
                copied = n
            },
            accept: { [self] block in
                deliveredBlocks &+= 1
                deliveredBytes &+= UInt64(copied)
                staging.withUnsafeBytes { raw in
                    consumer?.ringDelivered(UnsafeRawBufferPointer(rebasing: raw[0..<copied]),
                                            index: block.index,
                                            captureNs: block.captureNs)
                }
            })

        lappedBlocks &+= result.lappedBlocks
        tornBlocks &+= result.tornBlocks
        emptyBlocks &+= result.emptyBlocks
        let lost = result.lappedBlocks &+ result.tornBlocks &+ result.emptyBlocks
        if lost > 0 {
            consumer?.ringLost(blocks: lost, bytes: lost &* UInt64(blockBytes))
        }
        return result.terminated
    }
}

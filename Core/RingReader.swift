//
//  RingReader.swift — Lecture du ring partagé.
//
//  Le pilote publie ; l'application consomme. L'index de LECTURE est local à ce
//  lecteur et n'est jamais écrit dans la structure partagée : le pilote n'a ainsi
//  jamais à faire confiance à une valeur venue de l'espace utilisateur.
//
//  Discipline mémoire, côté pilote puis côté ici :
//      pilote : charge utile → validBytes/captureNs → writeIndex (RELEASE)
//      ici    : writeIndex (ACQUIRE) → charge utile
//  Sans ces barrières, le processeur peut réordonner les écritures et laisser
//  lire un bloc dont le contenu n'est pas encore arrivé — un défaut qui ne se
//  manifeste que sous charge, c'est-à-dire exactement à 20 Msps.
//
//  Ce lecteur est branché sur la mémoire réellement mappée par
//  IOConnectMapMemory64 (voir HackRFDriverKitTransport). Il reste exercé par les
//  tests et par le transport simulé, ce qui permet de valider la sémantique sans
//  matériel — et c'est ainsi qu'elle a été mise au point avant que le pilote ne
//  la produise.
//

import Foundation

/// Un bloc consommé.
struct RingBlock: Sendable {
    let index: UInt64
    let captureNs: UInt64
    let bytes: Int
}

/// Ce que le lecteur a observé sur un tour de drainage.
struct RingDrainResult: Sendable {
    var blocks: [RingBlock] = []
    var bytesRead: UInt64 = 0
    /// Blocs que le pilote a écrasés avant qu'on ne les lise. Compteur DISTINCT
    /// des erreurs USB : ici c'est nous qui sommes trop lents, pas le bus qui
    /// faute. Ne jamais additionner les deux.
    var lappedBlocks: UInt64 = 0
    /// Blocs copiés, puis écartés parce que le pilote a pu réécrire leur slot
    /// PENDANT la copie (voir `drainValidated`). Ils sont perdus au même titre
    /// que les blocs dépassés, et comptés à part pour qu'on sache lequel des
    /// deux mécanismes a joué.
    var tornBlocks: UInt64 = 0
    /// Blocs publiés vides (`validBytes == 0`) : le pilote publie ainsi un
    /// transfert en erreur pour conserver son rang. Aucune donnée n'y existe ;
    /// pour un enregistrement, c'est un trou.
    var emptyBlocks: UInt64 = 0
    var terminated = false
}

final class RingReader {

    /// Adresse de base du mappage, ou nil tant que l'étape 5 n'est pas là.
    private var base: UnsafeRawPointer?
    private var blockCount: UInt32 = 0
    private var blockBytes: UInt32 = 0
    private var headerBytes: UInt32 = UInt32(BHRF_RING_HEADER_BYTES)

    /// Index de lecture, purement local.
    private(set) var readIndex: UInt64 = 0
    private(set) var totalBytes: UInt64 = 0
    private(set) var totalLapped: UInt64 = 0

    init() {}

    /// Attache le lecteur à une mémoire mappée. Vérifie le magic et la version
    /// de disposition : un ring dont l'en-tête ne correspond pas est refusé, et
    /// certainement pas lu « au mieux ».
    func attach(base: UnsafeRawPointer, mappedBytes: Int) throws {
        let header = base.assumingMemoryBound(to: BHRFRingHeader.self).pointee
        guard header.magic == UInt32(BHRF_RING_MAGIC) else {
            throw TransportError.notSupportedYet("Ring inconnu (magic \(header.magic))")
        }
        guard header.layoutVersion == BHRF.protocolVersion else {
            throw TransportError.protocolMismatch(driver: header.layoutVersion,
                                                  app: BHRF.protocolVersion)
        }
        let needed = Int(header.headerBytes) + Int(header.blockCount) * Int(header.blockBytes)
        guard mappedBytes >= needed else {
            throw TransportError.unexpectedSize(expected: needed, got: mappedBytes)
        }
        self.base = base
        self.blockCount = header.blockCount
        self.blockBytes = header.blockBytes
        self.headerBytes = header.headerBytes
        self.readIndex = loadWriteIndexAcquire()
    }

    func detach() {
        base = nil
        blockCount = 0
        blockBytes = 0
    }

    var isAttached: Bool { base != nil }

    /// Lecture ACQUIRE de l'index d'écriture. L'ordre importe : cette lecture
    /// doit précéder toute lecture de charge utile.
    private func loadWriteIndexAcquire() -> UInt64 {
        guard let base else { return 0 }
        let p = base.advanced(by: MemoryLayout<BHRFRingHeader>.offset(of: \.writeIndex)!)
        let value = p.assumingMemoryBound(to: UInt64.self).pointee
        // Barrière d'acquisition : rien de ce qui suit ne peut être remonté
        // avant cette lecture.
        OSMemoryBarrier()
        return value
    }

    private func header() -> BHRFRingHeader? {
        guard let base else { return nil }
        return base.assumingMemoryBound(to: BHRFRingHeader.self).pointee
    }

    /// Consomme tout ce qui est disponible, jusqu'à `maxBlocks` par tour pour ne
    /// pas monopoliser la file.
    func drain(maxBlocks: Int = 64, sink: ((UnsafeRawBufferPointer) -> Void)? = nil)
        -> RingDrainResult {
        var result = RingDrainResult()
        guard let base, blockCount > 0 else { return result }

        let h = base.assumingMemoryBound(to: BHRFRingHeader.self).pointee
        if h.deviceState == UInt32(kBHRFDeviceStateTerminated.rawValue) {
            result.terminated = true
            return result
        }

        let write = loadWriteIndexAcquire()
        var available = write &- readIndex

        // Retard : le pilote a fait plus d'un tour d'avance. Les blocs perdus
        // sont COMPTÉS, jamais absorbés silencieusement — une capture trouée
        // qu'on présenterait comme complète serait pire qu'une capture manquée.
        if available > UInt64(blockCount) {
            let lapped = available - UInt64(blockCount)
            result.lappedBlocks = lapped
            totalLapped &+= lapped
            readIndex = write &- UInt64(blockCount)
            available = UInt64(blockCount)
        }

        var consumed = 0
        while consumed < maxBlocks, readIndex < write {
            let slot = Int(readIndex % UInt64(blockCount))
            let meta = withUnsafePointer(to: h.slots) { tuplePtr -> BHRFRingSlot in
                tuplePtr.withMemoryRebound(to: BHRFRingSlot.self,
                                           capacity: Int(BHRF_RING_MAX_SLOTS)) { $0[slot] }
            }
            let length = min(Int(meta.validBytes), Int(blockBytes))
            if length > 0 {
                let offset = Int(headerBytes) + slot * Int(blockBytes)
                let ptr = base.advanced(by: offset)
                sink?(UnsafeRawBufferPointer(start: ptr, count: length))
                result.bytesRead &+= UInt64(length)
                totalBytes &+= UInt64(length)
                result.blocks.append(RingBlock(index: readIndex,
                                               captureNs: meta.captureNs,
                                               bytes: length))
            }
            readIndex &+= 1
            consumed += 1
        }
        return result
    }

    /// Drainage dont chaque bloc livré est GARANTI intact.
    ///
    /// Pourquoi `drain` ne suffit pas pour un enregistrement : le pilote écrit
    /// par DMA directement dans les slots du ring, et il garde `inFlight`
    /// transferts en vol. Après avoir publié `writeIndex = n`, il soumet le
    /// transfert de l'index `n - 1 + inFlight` (voir `ReadComplete`). Les slots
    /// des index `[W, W + inFlight)` sont donc en cours d'écriture dès que
    /// l'index publié vaut `W`. Le bloc `i` occupe le même slot que l'index
    /// `i + C` ; il est écrasé dès que `W + inFlight - 1 ≥ i + C`.
    ///
    /// D'où la règle, exacte et non heuristique :
    ///
    ///     le bloc i copié est intact  ⇔  W relu APRÈS la copie ≤ i + C − inFlight
    ///
    /// `drain` ne compte comme perdu qu'un retard supérieur à `C` blocs. Entre
    /// `C − inFlight` et `C`, il copie un slot que le DMA peut réécrire au même
    /// instant, et livre un mélange de deux blocs sans rien signaler. Pour un
    /// affichage, c'est invisible ; pour un fichier déclaré fidèle, c'est une
    /// fausse preuve.
    ///
    /// - Parameters:
    ///   - inFlight: le nombre de transferts en vol côté pilote, tel que borné
    ///     par `bhrf_clamp_in_flight` (la même fonction C que le pilote).
    ///   - copy: recopie la charge utile hors du ring. Appelé AVANT validation.
    ///   - accept: reçoit le bloc une fois la copie validée, et seulement alors.
    func drainValidated(maxBlocks: Int = 64,
                        inFlight: UInt32,
                        copy: (UnsafeRawBufferPointer) -> Void,
                        accept: (RingBlock) -> Void) -> RingDrainResult {
        var result = RingDrainResult()
        guard let base, blockCount > 0 else { return result }

        let h = base.assumingMemoryBound(to: BHRFRingHeader.self).pointee
        if h.deviceState == UInt32(kBHRFDeviceStateTerminated.rawValue) {
            result.terminated = true
            return result
        }

        // La fenêtre sûre. `inFlight` ne peut pas atteindre `blockCount` (le
        // pilote le borne à C/2), mais une valeur aberrante ne doit pas faire
        // déborder le calcul : on garde toujours au moins un bloc lisible.
        let f = UInt64(min(inFlight, blockCount - 1))
        let window = UInt64(blockCount) - f

        let write = loadWriteIndexAcquire()
        let available = write &- readIndex
        if available > window {
            let lapped = available - window
            result.lappedBlocks = lapped
            totalLapped &+= lapped
            readIndex = write &- window
        }

        var consumed = 0
        while consumed < maxBlocks, readIndex < write {
            let index = readIndex
            let slot = Int(index % UInt64(blockCount))
            let meta = withUnsafePointer(to: h.slots) { tuplePtr -> BHRFRingSlot in
                tuplePtr.withMemoryRebound(to: BHRFRingSlot.self,
                                           capacity: Int(BHRF_RING_MAX_SLOTS)) { $0[slot] }
            }
            let length = min(Int(meta.validBytes), Int(blockBytes))
            readIndex &+= 1
            consumed += 1

            if length == 0 {
                result.emptyBlocks &+= 1
                continue
            }

            let offset = Int(headerBytes) + slot * Int(blockBytes)
            copy(UnsafeRawBufferPointer(start: base.advanced(by: offset), count: length))

            // Barrière COMPLÈTE avant de relire : les lectures de la copie ne
            // doivent pas pouvoir être retardées après cette relecture.
            OSMemoryBarrier()
            let after = loadWriteIndexAcquire()
            if after > index &+ window {
                result.tornBlocks &+= 1
                totalLapped &+= 1
                continue
            }

            result.bytesRead &+= UInt64(length)
            totalBytes &+= UInt64(length)
            let block = RingBlock(index: index, captureNs: meta.captureNs, bytes: length)
            result.blocks.append(block)
            accept(block)
        }
        return result
    }

    func reset() {
        readIndex = 0
        totalBytes = 0
        totalLapped = 0
    }
}

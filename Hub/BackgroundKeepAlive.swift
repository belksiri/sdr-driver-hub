//
//  BackgroundKeepAlive.swift — Garde le serveur vivant en arrière-plan.
//
//  iPadOS suspend une app en arrière-plan ; le serveur rtl_tcp doit pourtant
//  continuer quand Spectrum est au premier plan. Le mode d'arrière-plan
//  « audio » le permet tant qu'une session audio joue : on joue du silence,
//  MÉLANGÉ aux autres sons (.mixWithOthers), pour ne jamais couper l'audio de
//  Spectrum ni d'une autre app. Actif seulement pendant que le serveur écoute.
//

import AVFoundation

final class BackgroundKeepAlive {
    private let engine = AVAudioEngine()
    private let player = AVAudioPlayerNode()
    private var running = false

    func start() {
        guard !running else { return }
        do {
            let session = AVAudioSession.sharedInstance()
            try session.setCategory(.playback, options: [.mixWithOthers])
            try session.setActive(true)
            let format = AVAudioFormat(standardFormatWithSampleRate: 44_100, channels: 1)!
            let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: 4_410)!
            buffer.frameLength = buffer.frameCapacity      // zéros : silence
            if player.engine == nil {
                engine.attach(player)
                engine.connect(player, to: engine.mainMixerNode, format: format)
            }
            try engine.start()
            player.scheduleBuffer(buffer, at: nil, options: .loops)
            player.play()
            running = true
        } catch {
            NSLog("HUB: keep-alive audio failed — %@", "\(error)")
        }
    }

    func stop() {
        guard running else { return }
        player.stop()
        engine.stop()
        try? AVAudioSession.sharedInstance().setActive(false, options: [.notifyOthersOnDeactivation])
        running = false
    }
}

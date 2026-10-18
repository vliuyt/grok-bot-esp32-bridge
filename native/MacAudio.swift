import Foundation
import AVFoundation
import CoreAudio
import AudioToolbox

let outputLock = NSLock()
func emit(_ value: [String: Any]) {
    guard let data = try? JSONSerialization.data(withJSONObject: value) else { return }
    outputLock.lock(); defer { outputLock.unlock() }
    FileHandle.standardOutput.write(data); FileHandle.standardOutput.write(Data([10]))
}

func pcm(_ command: [String: Any]) -> Data? {
    guard let text = command["pcm"] as? String, let data = Data(base64Encoded: text),
          !data.isEmpty, data.count % 2 == 0, data.count <= 192000 else { return nil }
    return data
}

func channels(_ id: AudioObjectID, scope: AudioObjectPropertyScope) -> Int {
    var address = AudioObjectPropertyAddress(mSelector: kAudioDevicePropertyStreamConfiguration, mScope: scope, mElement: kAudioObjectPropertyElementMain)
    var size: UInt32 = 0
    guard AudioObjectGetPropertyDataSize(id, &address, 0, nil, &size) == noErr, size > 0 else { return 0 }
    let memory = UnsafeMutableRawPointer.allocate(byteCount: Int(size), alignment: MemoryLayout<AudioBufferList>.alignment)
    defer { memory.deallocate() }
    guard AudioObjectGetPropertyData(id, &address, 0, nil, &size, memory) == noErr else { return 0 }
    return UnsafeMutableAudioBufferListPointer(memory.assumingMemoryBound(to: AudioBufferList.self)).reduce(0) { $0 + Int($1.mNumberChannels) }
}

func devices() -> [[String: Any]] {
    var address = AudioObjectPropertyAddress(mSelector: kAudioHardwarePropertyDevices, mScope: kAudioObjectPropertyScopeGlobal, mElement: kAudioObjectPropertyElementMain)
    var size: UInt32 = 0
    guard AudioObjectGetPropertyDataSize(AudioObjectID(kAudioObjectSystemObject), &address, 0, nil, &size) == noErr else { return [] }
    var ids = [AudioObjectID](repeating: 0, count: Int(size) / MemoryLayout<AudioObjectID>.size)
    guard AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &address, 0, nil, &size, &ids) == noErr else { return [] }
    return ids.compactMap { id in
        var name: CFString = "" as CFString
        var propertySize = UInt32(MemoryLayout<CFString>.size)
        var property = AudioObjectPropertyAddress(mSelector: kAudioObjectPropertyName, mScope: kAudioObjectPropertyScopeGlobal, mElement: kAudioObjectPropertyElementMain)
        guard AudioObjectGetPropertyData(id, &property, 0, nil, &propertySize, &name) == noErr else { return nil }
        return ["id": id, "name": name as String, "inputChannels": channels(id, scope: kAudioObjectPropertyScopeInput), "outputChannels": channels(id, scope: kAudioObjectPropertyScopeOutput)]
    }
}

final class AudioBridge {
    let engine = AVAudioEngine()
    let player = AVAudioPlayerNode()
    let format = AVAudioFormat(standardFormatWithSampleRate: 24000, channels: 1)!
    let lock = NSLock()
    var capturing = false
    var captureTurn = 0
    var queuedFrames = 0
    var generation = 0

    func select(_ unit: AudioUnit?, option: String) throws {
        guard let index = CommandLine.arguments.firstIndex(of: option) else { return }
        guard index + 1 < CommandLine.arguments.count, var id = UInt32(CommandLine.arguments[index + 1]), let unit else { throw NSError(domain: "INVALID_DEVICE", code: 1) }
        let status = AudioUnitSetProperty(unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &id, UInt32(MemoryLayout<UInt32>.size))
        guard status == noErr else { throw NSError(domain: "DEVICE_SELECTION", code: Int(status)) }
    }

    func start() throws {
        try select(engine.outputNode.audioUnit, option: "--output")
        engine.attach(player)
        engine.connect(player, to: engine.mainMixerNode, format: format)
        if !CommandLine.arguments.contains("--playback-only") { try installCapture() }
        try engine.start(); player.play()
        emit(["type": "ready", "sampleRate": 24000, "channels": 1])
    }

    func installCapture() throws {
        let input = engine.inputNode
        try select(input.audioUnit, option: "--input")
        let source = input.outputFormat(forBus: 0)
        guard source.sampleRate > 0, let converter = AVAudioConverter(from: source, to: format) else { throw NSError(domain: "INPUT_FORMAT", code: 1) }
        input.installTap(onBus: 0, bufferSize: 2048, format: source) { [weak self] buffer, _ in
            guard let self else { return }
            self.lock.lock(); defer { self.lock.unlock() }
            guard self.capturing else { return }
            let turn = self.captureTurn
            let capacity = AVAudioFrameCount(ceil(Double(buffer.frameLength) * 24000 / source.sampleRate) + 8)
            guard let converted = AVAudioPCMBuffer(pcmFormat: self.format, frameCapacity: capacity) else { return }
            var used = false
            var error: NSError?
            let result = converter.convert(to: converted, error: &error) { _, status in
                if used { status.pointee = .noDataNow; return nil }
                used = true; status.pointee = .haveData; return buffer
            }
            guard error == nil, result != .error, let samples = converted.floatChannelData?[0] else {
                emit(["type": "fault", "code": "INPUT_CONVERSION"]); return
            }
            var data = Data(capacity: Int(converted.frameLength) * 2)
            for index in 0..<Int(converted.frameLength) {
                var sample = Int16(max(-32768, min(32767, Int((samples[index] * 32767).rounded())))).littleEndian
                withUnsafeBytes(of: &sample) { data.append(contentsOf: $0) }
            }
            if !data.isEmpty { emit(["type": "capture", "pcm": data.base64EncodedString(), "turn": turn]) }
        }
    }

    func clear() {
        generation += 1; player.stop(); queuedFrames = 0; player.play()
        emit(["type": "cleared"])
    }

    func handle(_ command: [String: Any]) {
        switch command["type"] as? String {
        case "record.start":
            clear(); lock.lock(); captureTurn = command["turn"] as? Int ?? 0; capturing = true; lock.unlock()
        case "record.stop":
            lock.lock(); capturing = false; let turn = captureTurn; lock.unlock()
            emit(["type": "recordingStopped", "turn": turn])
        case "clear": clear()
        case "play":
            guard let data = pcm(command) else { emit(["type": "fault", "code": "INVALID_PCM"]); return }
            let frames = data.count / 2
            guard queuedFrames + frames <= 24000 * 30 else { clear(); emit(["type": "fault", "code": "PLAYBACK_OVERFLOW"]); return }
            guard let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: AVAudioFrameCount(frames)), let samples = buffer.floatChannelData?[0] else { return }
            buffer.frameLength = AVAudioFrameCount(frames)
            data.withUnsafeBytes { raw in
                for index in 0..<frames { samples[index] = Float(Int16(littleEndian: raw.loadUnaligned(fromByteOffset: index * 2, as: Int16.self))) / 32768 }
            }
            queuedFrames += frames; let current = generation
            player.scheduleBuffer(buffer, completionCallbackType: .dataPlayedBack) { [weak self] _ in
                DispatchQueue.main.async {
                    guard let self, self.generation == current else { return }
                    self.queuedFrames -= frames
                    if self.queuedFrames == 0 { emit(["type": "drained"]) }
                }
            }
        case "quit": engine.stop(); exit(0)
        default: emit(["type": "fault", "code": "UNKNOWN_COMMAND"])
        }
    }
}

if CommandLine.arguments.contains("--devices") {
    emit(["devices": devices()])
} else if CommandLine.arguments.contains("--validate") {
    while let line = readLine() {
        if let data = line.data(using: .utf8), let command = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any], let audio = pcm(command) {
            emit(["type": "accepted", "frames": audio.count / 2])
        } else { emit(["type": "fault", "code": "INVALID_PCM"]) }
    }
} else {
    let bridge = AudioBridge()
    func begin() {
        do { try bridge.start() }
        catch { emit(["type": "fault", "code": "AUDIO_START_FAILED"]); exit(2) }
        DispatchQueue.global().async {
            while let line = readLine() {
                guard line.utf8.count <= 300000, let data = line.data(using: .utf8),
                      let command = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any] else {
                    emit(["type": "fault", "code": "INVALID_COMMAND"]); continue
                }
                DispatchQueue.main.async { bridge.handle(command) }
            }
            DispatchQueue.main.async { bridge.engine.stop(); exit(0) }
        }
    }
    if CommandLine.arguments.contains("--playback-only") { begin() }
    else { switch AVCaptureDevice.authorizationStatus(for: .audio) {
    case .authorized: begin()
    case .notDetermined:
        AVCaptureDevice.requestAccess(for: .audio) { allowed in
            DispatchQueue.main.async {
                if allowed { begin() } else { emit(["type": "fault", "code": "MICROPHONE_DENIED"]); exit(2) }
            }
        }
    default: emit(["type": "fault", "code": "MICROPHONE_DENIED"]); exit(2)
    } }
    RunLoop.main.run()
}

import Foundation

/// Bounded FIFO between the ring drain (producer) and the TCP connection
/// (consumer). A slow client never blocks the drain: when the queue is full
/// the newest block is DROPPED and counted, and the client is told nothing
/// silently — the loss shows in the counters.
public final class IqSendQueue {
    public let maxBytes: Int
    private var items = [Data]()
    private var head = 0
    private var queued = 0
    private let lock = NSLock()
    public private(set) var droppedBlocks: UInt64 = 0
    public private(set) var droppedBytes: UInt64 = 0
    public private(set) var enqueuedBytes: UInt64 = 0

    public init(maxBytes: Int = 8 << 20) { self.maxBytes = maxBytes }

    public var queuedBytes: Int { lock.lock(); defer { lock.unlock() }; return queued }

    @discardableResult
    public func push(_ d: Data) -> Bool {
        lock.lock(); defer { lock.unlock() }
        if queued + d.count > maxBytes {
            droppedBlocks += 1
            droppedBytes += UInt64(d.count)
            return false
        }
        items.append(d)
        queued += d.count
        enqueuedBytes += UInt64(d.count)
        return true
    }

    /// Up to [maxBytes] bytes of whole blocks, in order; nil when empty.
    public func pop(upTo limit: Int) -> Data? {
        lock.lock(); defer { lock.unlock() }
        guard head < items.count else { return nil }
        var out = Data()
        while head < items.count, out.isEmpty || out.count + items[head].count <= limit {
            out.append(items[head])
            queued -= items[head].count
            head += 1
        }
        if head > 256 { items.removeFirst(head); head = 0 }
        return out
    }

    public func clear() {
        lock.lock(); defer { lock.unlock() }
        items.removeAll(); head = 0; queued = 0
    }

    public func resetCounters() {
        lock.lock(); defer { lock.unlock() }
        droppedBlocks = 0; droppedBytes = 0; enqueuedBytes = 0
    }
}

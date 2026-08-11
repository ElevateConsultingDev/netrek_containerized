import SwiftUI
import CoreGraphics

/// Manages the COW engine thread and publishes frame snapshots for SwiftUI.
class EngineController: ObservableObject {

    struct WindowFrame: Identifiable {
        let id: Int
        let image: CGImage
        let x: Int
        let y: Int
        let width: Int
        let height: Int
    }

    @Published var frames: [WindowFrame] = []
    @Published var totalWidth: Int = 1024
    @Published var totalHeight: Int = 768
    @Published var isRunning = false

    private var displayLink: Timer?
    private var lastVersion: UInt64 = 0

    /// Start the engine with the given server arguments
    func start(host: String = "localhost", port: Int = 2592) {
        guard !isRunning else { return }
        isRunning = true

        // Build argv for cowmain
        let args = ["netrek-swiftui", "-h", host, "-p", String(port)]
        let argc = Int32(args.count)

        // Create C string array that persists
        let cStrings = args.map { strdup($0) }
        let argv = UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>.allocate(
            capacity: args.count + 1)
        for i in 0..<args.count {
            argv[i] = cStrings[i]
        }
        argv[args.count] = nil

        // Start the C engine on a background thread
        engine_start(argc, argv)

        // Start polling for frame updates
        startDisplayLink()
    }

    private func startDisplayLink() {
        displayLink = Timer.scheduledTimer(withTimeInterval: 1.0/60.0, repeats: true) { [weak self] _ in
            self?.pollSnapshots()
        }
    }

    /// Read snapshot data from C and update published frames
    private func pollSnapshots() {
        pollCount += 1

        pthread_mutex_lock(&snapshot_mutex)

        let version = snapshot_version
        guard version != lastVersion else {
            pthread_mutex_unlock(&snapshot_mutex)
            // Log periodically when no updates detected
            if pollCount % 300 == 0 {
                NSLog("[EngineController] poll #%d: no change (version=%llu, count=%d)",
                      pollCount, version, snapshot_count)
            }
            return
        }

        let oldVersion = lastVersion
        lastVersion = version

        let count = Int(snapshot_count)
        var newFrames: [WindowFrame] = []
        var maxX = 0, maxY = 0
        var mappedCount = 0
        var imgCount = 0

        for i in 0..<count {
            var x: Int32 = 0, y: Int32 = 0, w: Int32 = 0, h: Int32 = 0, mapped: Int32 = 0
            swiftui_get_window_info(Int32(i), &x, &y, &w, &h, &mapped)

            guard mapped != 0 else { continue }
            mappedCount += 1

            let imgPtr = withUnsafePointer(to: &snapshots) { ptr -> CGImage? in
                let base = UnsafeRawPointer(ptr).assumingMemoryBound(to: CGImage?.self)
                return base[i]
            }
            guard let img = imgPtr else { continue }
            imgCount += 1

            newFrames.append(WindowFrame(
                id: i, image: img,
                x: Int(x), y: Int(y), width: Int(w), height: Int(h)))

            let right = Int(x + w)
            let bottom = Int(y + h)
            if right > maxX { maxX = right }
            if bottom > maxY { maxY = bottom }
        }

        pthread_mutex_unlock(&snapshot_mutex)

        // Log first few updates and then periodically
        if updateCount < 5 || updateCount % 300 == 0 {
            NSLog("[EngineController] update #%d: version %llu→%llu, windows=%d mapped=%d images=%d frames=%d",
                  updateCount, oldVersion, version, count, mappedCount, imgCount, newFrames.count)
        }
        updateCount += 1

        self.frames = newFrames
        if maxX > 0 { self.totalWidth = maxX }
        if maxY > 0 { self.totalHeight = maxY }
    }

    private var pollCount = 0
    private var updateCount = 0

    deinit {
        displayLink?.invalidate()
    }
}

// MARK: - C callback: called from W_Flush() on the engine thread

@_cdecl("swiftui_notify_flush")
func swiftuiNotifyFlush() {
    // Timer-based polling handles updates
}

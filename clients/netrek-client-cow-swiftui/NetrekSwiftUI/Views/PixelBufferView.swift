import SwiftUI

/// Displays a CGImage from a C window's pixel buffer.
/// Handles mouse and keyboard events, forwarding them to the C engine.
struct PixelBufferView: NSViewRepresentable {
    let image: CGImage
    let windowID: Int
    let windowWidth: Int
    let windowHeight: Int

    func makeNSView(context: Context) -> PixelBufferNSView {
        let view = PixelBufferNSView()
        view.windowID = windowID
        view.wantsLayer = true
        return view
    }

    func updateNSView(_ nsView: PixelBufferNSView, context: Context) {
        nsView.currentImage = image
        nsView.windowID = windowID
        nsView.layer?.setNeedsDisplay()
        nsView.needsDisplay = true
    }
}

/// NSView that renders a CGImage and handles input events.
class PixelBufferNSView: NSView {
    var currentImage: CGImage?
    var windowID: Int = -1

    override var acceptsFirstResponder: Bool { true }
    override var isFlipped: Bool { true }
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    override func draw(_ dirtyRect: NSRect) {
        guard let ctx = NSGraphicsContext.current?.cgContext,
              let image = currentImage else { return }

        // CGContextDrawImage draws images upside-down in a flipped NSView.
        // Apply an additional flip to compensate, so the image renders correctly.
        ctx.saveGState()
        ctx.interpolationQuality = .none
        ctx.translateBy(x: 0, y: bounds.height)
        ctx.scaleBy(x: 1, y: -1)
        ctx.draw(image, in: bounds)
        ctx.restoreGState()
    }

    // MARK: - Keyboard events

    override func keyDown(with event: NSEvent) {
        guard let chars = event.charactersIgnoringModifiers,
              let char = chars.first else { return }

        let key = mapKeyToWlib(char, event: event)
        if key == 0 { return }

        var modifier: Int32 = 0
        if event.modifierFlags.contains(.shift) { modifier |= 0x08 }
        if event.modifierFlags.contains(.control) { modifier |= 0x10 }

        let loc = convert(event.locationInWindow, from: nil)
        swiftui_inject_key(Int32(windowID), UInt8(key), Int32(loc.x),
                          Int32(loc.y), 2, modifier)  // 2 = W_EV_KEY
    }

    // MARK: - Mouse events

    override func mouseDown(with event: NSEvent) {
        handleMouseButton(event, button: 1)  // W_LBUTTON
    }

    override func rightMouseDown(with event: NSEvent) {
        handleMouseButton(event, button: 3)  // W_RBUTTON
    }

    override func otherMouseDown(with event: NSEvent) {
        handleMouseButton(event, button: 2)  // W_MBUTTON
    }

    override func mouseDragged(with event: NSEvent) {
        handleMouseMotion(event, button: 1)
    }

    override func rightMouseDragged(with event: NSEvent) {
        handleMouseMotion(event, button: 3)
    }

    override func scrollWheel(with event: NSEvent) {
        let loc = convert(event.locationInWindow, from: nil)
        let button: Int32 = event.deltaY > 0 ? 4 : 5  // W_WUBUTTON / W_WDBUTTON
        swiftui_inject_button(Int32(windowID), button,
                             Int32(loc.x), Int32(loc.y), 0)
    }

    private func handleMouseButton(_ event: NSEvent, button: Int32) {
        let loc = convert(event.locationInWindow, from: nil)
        var mod: Int32 = 0
        if event.modifierFlags.contains(.shift) { mod |= 0x08 }
        if event.modifierFlags.contains(.control) { mod |= 0x10 }

        swiftui_inject_button(Int32(windowID), button | mod,
                             Int32(loc.x), Int32(loc.y), mod)
    }

    private func handleMouseMotion(_ event: NSEvent, button: Int32) {
        let loc = convert(event.locationInWindow, from: nil)
        swiftui_inject_key(Int32(windowID), UInt8(button),
                          Int32(loc.x), Int32(loc.y), 5, 0)  // 5 = W_EV_CM_BUTTON
    }

    // MARK: - Key mapping

    private func mapKeyToWlib(_ char: Character, event: NSEvent) -> UInt8 {
        let keyCode = event.keyCode
        let mods = event.modifierFlags

        // Arrow keys
        if keyCode == 126 { return 1 }  // Up = W_Key_Up
        if keyCode == 125 { return 2 }  // Down = W_Key_Down

        // Get the ASCII value
        guard let ascii = char.asciiValue else { return 0 }

        // Control key handling
        if mods.contains(.control) && ascii >= 0x40 && ascii <= 0x7f {
            return ascii & 0x1f
        }

        return ascii
    }
}

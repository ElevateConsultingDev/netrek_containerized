import SwiftUI

/// Main layout container.
/// Composites all virtual COW windows into a single view, matching the
/// positions calculated by the C engine's newwin.c layout.
struct ContentView: View {
    @EnvironmentObject var engine: EngineController
    @State private var host = "localhost"
    @State private var port = "2692"
    @State private var started = false

    var body: some View {
        if !started {
            connectView
        } else {
            gameView
        }
    }

    // MARK: - Connection screen

    private var connectView: some View {
        VStack(spacing: 16) {
            Text("Netrek")
                .font(.largeTitle)
                .fontWeight(.bold)

            HStack {
                Text("Server:")
                TextField("hostname", text: $host)
                    .textFieldStyle(.roundedBorder)
                    .frame(width: 200)
                Text("Port:")
                TextField("port", text: $port)
                    .textFieldStyle(.roundedBorder)
                    .frame(width: 80)
            }

            Button("Connect") {
                let p = Int(port) ?? 2592
                engine.start(host: host, port: p)
                started = true
            }
            .keyboardShortcut(.return, modifiers: [])
            .buttonStyle(.borderedProminent)
        }
        .padding(40)
        .frame(minWidth: 400, minHeight: 200)
    }

    // MARK: - Game view: composite all virtual windows

    private var gameView: some View {
        ZStack(alignment: .topLeading) {
            Color.black

            ForEach(engine.frames) { frame in
                PixelBufferView(
                    image: frame.image,
                    windowID: frame.id,
                    windowWidth: frame.width,
                    windowHeight: frame.height
                )
                .frame(width: CGFloat(frame.width),
                       height: CGFloat(frame.height))
                .offset(x: CGFloat(frame.x), y: CGFloat(frame.y))
            }
        }
        .frame(width: CGFloat(engine.totalWidth),
               height: CGFloat(engine.totalHeight))
        .focusable()
    }
}

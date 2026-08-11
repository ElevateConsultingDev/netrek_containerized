import SwiftUI

@main
struct NetrekApp: App {
    @StateObject private var engine = EngineController()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(engine)
        }
        .windowResizability(.contentSize)
    }
}

import SwiftUI

@main
struct OpenGameAccessApp: App {
    @StateObject private var session = GameSession()
    @StateObject private var speech = SpeechEngine()

    var body: some Scene {
        WindowGroup {
            RootView()
                .environmentObject(session)
                .environmentObject(speech)
                .onAppear {
                    session.attach(to: speech)
                }
        }
    }
}

import SwiftUI

@main
struct PokemonAccessApp: App {
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

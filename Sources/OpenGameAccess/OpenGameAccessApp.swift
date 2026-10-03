import SwiftUI

@main
struct OpenGameAccessApp: App {
    @StateObject private var session = GameSession()
    @StateObject private var speech = SpeechEngine()
    @StateObject private var controllers = ControllerInput()

    var body: some Scene {
        WindowGroup {
            RootView()
                .environmentObject(session)
                .environmentObject(speech)
                .environmentObject(controllers)
                .onAppear {
                    session.attach(to: speech)
                    controllers.session = session
                    controllers.speech = speech
                }
        }
    }
}

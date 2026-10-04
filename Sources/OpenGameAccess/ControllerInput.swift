import Foundation
import GameController

/// Physical game controllers (MFi, DualSense/DualShock, Xbox, …): the console's
/// buttons, plus the reader's commands as trigger chords.
///
/// Game presses go through `InputBridge.setButton` — the same path as the
/// on-screen pad — so the reader's echoes (tracked menus, the PSP OSK) see a
/// controller press exactly as they see a tap. While a controller is connected
/// the on-screen game pad is hidden (`connectedName != nil`).
///
/// Face buttons map BY POSITION to the emulated console's layout, not by the
/// letter printed on the controller: on the DS and Game Boy the right button is
/// A and the bottom one B (Nintendo's layout); on the PSP the four positions are
/// Cross/Circle/Triangle/Square exactly as on a PlayStation controller.
///
/// L2 and R2 never reach the game: they are the reader's modifiers. While one is
/// held, D-pad and face presses become reader commands instead of game input,
/// laid out so each hand keeps one job:
///
///   L2 ("ask" — index on L2, right thumb on the face buttons)
///     south (Cross / B position)  Where am I
///     east  (Circle / A position) Stop speech
///     north (Triangle / X)        Repeat
///     west  (Square / Y)          Find path
///     D-pad down                  Tiles around you
///   R2 ("browse" — index on R2, left thumb on the D-pad)
///     D-pad left / right          Previous / next item (allies, list entries)
///     D-pad up / down             Previous / next group (enemies, list groups)
///     south                       Read item again (next waiting ally on FE)
///     north / west / east         The game's own toggles (Dissidia: Customize,
///                                 Character, Name entry)
///
/// Each chord uses the game's native reader when it has that command, else the
/// Pokémon script key, else says there is nothing there — never silence.
/// docs/controllers.md is the player-facing copy of this table.
@MainActor
final class ControllerInput: ObservableObject {
    /// Name of the connected controller, nil when none — drives the UI.
    @Published private(set) var connectedName: String?

    /// Asked at press time, so mappings follow the loaded game.
    weak var session: GameSession?
    weak var speech: SpeechEngine?

    /// Which emulated button each currently-held physical input pressed.
    /// Release uses this, not the current mapping, so a game switch or a
    /// modifier change mid-press cannot leave a key stuck down.
    private var active: [String: GamePadButton] = [:]
    /// Physical inputs holding each emulated button down (D-pad and stick
    /// both drive directions).
    private var holders: [GamePadButton: Set<String>] = [:]
    /// Held modifier triggers, by source (several controllers may be bound).
    private var askHeld: Set<String> = []
    private var browseHeld: Set<String> = []
    private var observers: [NSObjectProtocol] = []

    private static let stickThreshold: Float = 0.5

    init() {
        let center = NotificationCenter.default
        observers.append(center.addObserver(forName: .GCControllerDidConnect,
                                            object: nil, queue: .main) { [weak self] _ in
            MainActor.assumeIsolated { self?.refresh() }
        })
        observers.append(center.addObserver(forName: .GCControllerDidDisconnect,
                                            object: nil, queue: .main) { [weak self] _ in
            MainActor.assumeIsolated { self?.refresh() }
        })
        refresh()
    }

    /// Re-scan: bind every gamepad, and publish the first one's name.
    private func refresh() {
        let pads = GCController.controllers().filter { $0.extendedGamepad != nil }
        for controller in pads { bind(controller) }
        let name = pads.first.map { $0.vendorName ?? "Game controller" }
        if name == nil, connectedName != nil { releaseAll() }
        connectedName = name
    }

    // MARK: - Binding

    private enum Pad { case up, down, left, right }
    private enum Face { case south, east, west, north }

    private func bind(_ controller: GCController) {
        guard let pad = controller.extendedGamepad else { return }
        // Handlers run on the main queue (GCController's default handlerQueue).
        let id = ObjectIdentifier(controller).hashValue

        func onPress(_ input: GCControllerButtonInput?, _ name: String,
                     _ handle: @escaping @MainActor (ControllerInput, String, Bool) -> Void) {
            input?.pressedChangedHandler = { [weak self] _, _, pressed in
                MainActor.assumeIsolated {
                    guard let self else { return }
                    handle(self, "\(id).\(name)", pressed)
                }
            }
        }

        onPress(pad.dpad.up, "dpad.up") { $0.direction(.up, $1, $2) }
        onPress(pad.dpad.down, "dpad.down") { $0.direction(.down, $1, $2) }
        onPress(pad.dpad.left, "dpad.left") { $0.direction(.left, $1, $2) }
        onPress(pad.dpad.right, "dpad.right") { $0.direction(.right, $1, $2) }

        onPress(pad.buttonA, "south") { $0.faceButton(.south, $1, $2) }
        onPress(pad.buttonB, "east") { $0.faceButton(.east, $1, $2) }
        onPress(pad.buttonX, "west") { $0.faceButton(.west, $1, $2) }
        onPress(pad.buttonY, "north") { $0.faceButton(.north, $1, $2) }

        onPress(pad.leftShoulder, "l1") { $0.set($1, .l, pressed: $2) }
        onPress(pad.rightShoulder, "r1") { $0.set($1, .r, pressed: $2) }
        onPress(pad.buttonMenu, "menu") { $0.set($1, .start, pressed: $2) }
        onPress(pad.buttonOptions, "options") { $0.set($1, .select, pressed: $2) }

        // The reader's modifiers. Never game input.
        onPress(pad.leftTrigger, "l2") { s, source, pressed in
            if pressed { s.askHeld.insert(source) } else { s.askHeld.remove(source) }
        }
        onPress(pad.rightTrigger, "r2") { s, source, pressed in
            if pressed { s.browseHeld.insert(source) } else { s.browseHeld.remove(source) }
        }

        // Left stick as a second D-pad, with a deadzone so drift never walks.
        pad.leftThumbstick.valueChangedHandler = { [weak self] _, x, y in
            MainActor.assumeIsolated {
                guard let self else { return }
                let t = Self.stickThreshold
                self.direction(.up, "\(id).stick.up", y > t)
                self.direction(.down, "\(id).stick.down", y < -t)
                self.direction(.left, "\(id).stick.left", x < -t)
                self.direction(.right, "\(id).stick.right", x > t)
            }
        }
    }

    // MARK: - Routing

    private func direction(_ d: Pad, _ source: String, _ pressed: Bool) {
        if pressed, active[source] == nil, let slot = chord(for: .pad(d)) {
            perform(slot)   // consumed: the game never sees this press
            return
        }
        let button: GamePadButton
        switch d {
        case .up: button = .up
        case .down: button = .down
        case .left: button = .left
        case .right: button = .right
        }
        set(source, button, pressed: pressed)
    }

    private func faceButton(_ f: Face, _ source: String, _ pressed: Bool) {
        if pressed, active[source] == nil, let slot = chord(for: .face(f)) {
            perform(slot)
            return
        }
        set(source, pressed ? gameButton(for: f) : nil, pressed: pressed)
    }

    /// Maps a physical controller face button to the emulated pad position.
    ///
    /// ⛔ THE LAYOUT IS A PROPERTY OF THE PAD THE PLAYER IS HOLDING, and it now
    /// comes from the registry's FACE-BUTTON LIST rather than a switch over
    /// three console cases. The registry already says a Game Boy has two face
    /// buttons and a PSP calls them Cross/Circle/Triangle/Square, so the
    /// physical mapping is derived from that instead of restating it. A console
    /// added to the registry is playable with a controller without touching
    /// this file.
    private func gameButton(for position: Face) -> GamePadButton? {
        let held = (session?.system ?? GameSystem.DSDefault).faceButtons.map(\.title)

        // PSP names its four the Sony way, in the SAME physical positions the
        // core maps to its A/B/X/Y slots.
        if held.contains("Cross") {
            switch position {
            case .south: return .a
            case .east:  return .b
            case .north: return .x
            case .west:  return .y
            }
        }

        // The DS/east-Asian order: A is the east button, B the south.
        switch position {
        case .east:  return .a
        case .south: return .b
        case .north: return held.count >= 4 ? .x : nil
        case .west:  return held.count >= 4 ? .y : nil
        }
    }

    /// Edge-only: an emulated button goes down when its first holder presses
    /// and up when its last holder releases.
    private func set(_ source: String, _ button: GamePadButton?, pressed: Bool) {
        if pressed {
            guard active[source] == nil, let button else { return }
            active[source] = button
            let wasDown = !(holders[button]?.isEmpty ?? true)
            holders[button, default: []].insert(source)
            if !wasDown { InputBridge.setButton(button, down: true) }
        } else {
            guard let button = active.removeValue(forKey: source) else { return }
            holders[button]?.remove(source)
            if holders[button]?.isEmpty ?? true { InputBridge.setButton(button, down: false) }
        }
    }

    private func releaseAll() {
        for button in holders.keys where !(holders[button]?.isEmpty ?? true) {
            InputBridge.setButton(button, down: false)
        }
        holders.removeAll()
        active.removeAll()
        askHeld.removeAll()
        browseHeld.removeAll()
    }

    // MARK: - Reader chords

    private enum Input { case pad(Pad), face(Face) }

    /// The reader's command slots — what a chord means, before it is resolved
    /// against the loaded game.
    private enum Slot {
        case whereAmI, stopSpeech, repeatLast, findPath, tiles
        case prevItem, nextItem, prevGroup, nextGroup, readItem
        case gameToggle1, gameToggle2, gameToggle3
    }

    /// Nil when no modifier is held (the press is game input), or for an
    /// unassigned combination (also passed through — L2 + D-pad left still
    /// moves, rather than doing nothing).
    private func chord(for input: Input) -> Slot? {
        if !askHeld.isEmpty {
            switch input {
            case .face(.south): return .whereAmI
            case .face(.east):  return .stopSpeech
            case .face(.north): return .repeatLast
            case .face(.west):  return .findPath
            case .pad(.down):   return .tiles
            default: return nil
            }
        }
        if !browseHeld.isEmpty {
            switch input {
            case .pad(.left):   return .prevItem
            case .pad(.right):  return .nextItem
            case .pad(.up):     return .prevGroup
            case .pad(.down):   return .nextGroup
            case .face(.south): return .readItem
            case .face(.north): return .gameToggle1
            case .face(.west):  return .gameToggle2
            case .face(.east):  return .gameToggle3
            }
        }
        return nil
    }

    private enum Action {
        case adapter(AdapterCommand)
        case hotkey(String)
        case stopSpeech
        case repeatLast
    }

    /// Native reader first, then the Pokémon script key — the same buttons the
    /// on-screen reader groups offer for this game, and only those.
    private func resolve(_ slot: Slot) -> Action? {
        guard let session else { return nil }
        let native = Set(session.availableAdapterCommands)
        let system = session.system ?? GameSystem.DSDefault
        let luaKeys: Set<String> = session.isPokemonROM
            ? Set((system.pathKeys + system.readingKeys + system.speechKeys).map(\.key))
            : []
        func cmd(_ c: AdapterCommand) -> Action? { native.contains(c) ? .adapter(c) : nil }
        func key(_ k: String) -> Action? { luaKeys.contains(k) ? .hotkey(k) : nil }

        switch slot {
        case .whereAmI:    return cmd(.whereAmI) ?? key("C") ?? key("M")
        // The DS script's own stop key also clears its queue; elsewhere the
        // engine stop is what makes silence stick (see SpeechGroup).
        case .stopSpeech:  return key("R") ?? .stopSpeech
        case .repeatLast:  return key("U") ?? .repeatLast
        case .findPath:    return key("P")
        case .tiles:       return key("E")
        case .prevItem:    return cmd(.prevAlly) ?? key("J")
        case .nextItem:    return cmd(.nextAlly) ?? key("L")
        case .prevGroup:   return cmd(.prevEnemy) ?? key("I")
        case .nextGroup:   return cmd(.nextEnemy) ?? key("O")
        case .readItem:    return cmd(.nextUnactedAlly) ?? key("K")
        case .gameToggle1: return cmd(.custToggle)
        case .gameToggle2: return cmd(.charToggle)
        case .gameToggle3: return cmd(.oskToggle)
        }
    }

    private func perform(_ slot: Slot) {
        guard let action = resolve(slot) else {
            // A chord with nothing behind it must say so: silence reads as a
            // broken controller.
            speech?.announce("No reader command there for this game.")
            return
        }
        switch action {
        case .adapter(let command): session?.sendAdapterCommand(command)
        case .hotkey(let key):      InputBridge.tapHotkey(key)
        case .stopSpeech:           session?.stopSpeech()
        case .repeatLast:
            guard let speech else { return }
            let last = speech.lastSpoken
            speech.announce(last.isEmpty ? "Nothing to repeat." : last)
        }
    }
}

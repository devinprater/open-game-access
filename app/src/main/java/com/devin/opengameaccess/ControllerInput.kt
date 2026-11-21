package com.devin.opengameaccess

import android.content.Context
import android.hardware.input.InputManager
import android.os.Handler
import android.os.Looper
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import me.magnum.melonds.accessibility.AccessibilityScript

/**
 * Physical game controllers (Xbox, PlayStation, generic Bluetooth/USB
 * pads): the console's buttons, plus the reader's commands as trigger
 * chords.
 *
 * L2 and R2 never reach the game: they are the reader's modifiers.
 * While one is held, D-pad and face presses become reader commands
 * instead of game input, laid out so each hand keeps one job (the table
 * is iOS's, unchanged):
 *
 *   L2 ("ask" — index on L2, right thumb on the face buttons)
 *     south (Cross / B position)  Where am I
 *     east  (Circle / A position) Stop speech
 *     north (Triangle / X)        Repeat
 *     west  (Square / Y)          Find path
 *     D-pad down                  Tiles around you
 *     D-pad up                    Previous spoken line (walk back)
 *   R2 ("browse" — index on R2, left thumb on the D-pad)
 *     D-pad left / right          Previous / next item (allies, entries)
 *     D-pad up / down             Previous / next group (enemies, groups)
 *     south                       Read item again
 *     north / west / east         The game's own toggles (Dissidia:
 *                                 Customize, Character, Name entry)
 *
 * The stick clicks are the reader's two most-used commands outright:
 * L3 = Where Am I, R3 = Menu status. Each chord uses the game's native
 * reader when it has that command, else the Pokémon script key, else
 * says there is nothing there — never silence.
 *
 * ⛔ THIS IS A PORT OF iOS's ControllerInput.swift, BEHAVIOUR FIRST. Game
 * presses go through the sessions' existing key plumbing
 * (GbGameSession.pressKey / PspGameSession.pressButton — the same calls
 * the on-screen pad makes), so the reader's echoes (tracked menus, the
 * PSP OSK) see a controller press exactly as they see a tap. There is no
 * parallel input system. While a controller is connected the on-screen
 * game pad is hidden (the launcher reads [connectedName]).
 *
 * A physical keyboard plays too — the docked setup: the phone on a
 * USB-C dock, a keyboard in front of the player. While a session runs
 * its keys route through this same layer and the same bookkeeping (the
 * source strings already name the device, so a keyboard unplugged
 * mid-press releases exactly what it held, like a pad unplugged). The
 * layout is the classic emulator one (the ZSNES/VBA convention):
 *
 *   Arrow keys           the D-pad
 *   Z / X / A / S        the face diamond, BY POSITION: Z south,
 *                        X east, A west, S north
 *   Q / W                L1 / R1
 *   Enter                Start
 *   J / K / L            the reader's items: previous / current /
 *                        next — the browse chords' commands on
 *                        dedicated keys; with Shift held, J and L
 *                        change category (the browse groups) instead
 *   P                    Find path (the ask chord's command)
 *   Shift                Select, fired on release: a tap only — once
 *                        J or L has used the press as the category
 *                        modifier, it is spent and no Select fires
 *   Space / Tab          Where Am I / Menu status — the stick clicks'
 *                        commands, press-only (Tab is consumed
 *                        in-session, so focus never walks the screen
 *                        mid-game)
 *
 * The keyboard gets no modifier keys of its own: of the pad's chords,
 * the browse set's item and group commands and Find path have keys
 * above. The rest — Stop speech, Repeat, the spoken-history walk,
 * Tiles around you, and the browse face toggles — stay pad-only
 * until keys are assigned to them.
 *
 * A keyboard is not a controller here: it never sets [connectedName],
 * so the on-screen pad stays put and nothing calls it a pad. It is
 * accepted on its own device test below — never through the pad's.
 *
 * Face buttons map BY POSITION to the emulated console's layout, not by
 * the letter printed on the controller: on the Game Boy the right button
 * is A and the bottom one B (Nintendo's layout); on the PSP the four
 * positions are Cross/Circle/Triangle/Square exactly as on a PlayStation
 * controller. Android's standard gamepad keycodes are already positional
 * (BUTTON_A is the bottom button, BUTTON_B the right one, whatever the
 * pad's brand), so the position mapping survives the platform change.
 *
 * Genesis joins the funnel with the parity program's Genesis stage:
 * the positions land on the Genesis pad the way the shared registry's
 * table prescribes (west = A, south = B, east = C, north = Y; L1 = X,
 * R1 = Z; Select = Mode), so one physical pad drives it like every
 * other console. The DS session does NOT route here (it never joined
 * this layer); Genesis does because its pad is the whole of its
 * input — it has no reader hotkeys or touch surface to keep apart.
 * Its reader chords resolve the honest way: with no Genesis reader,
 * the ask/browse slots have nothing behind them and say so, while
 * Stop speech and the Repeats are bridge-level and work as elsewhere.
 *
 * The Activity is the single input funnel: MainActivity's
 * dispatchKeyEvent / onGenericMotionEvent hand gamepad-sourced events
 * here first, and only while a game session is running — at the launcher
 * the pad does nothing and nothing is consumed, so TalkBack and keyboard
 * navigation keep their keys everywhere outside a game.
 */
object ControllerInput {

    /** Name of the connected controller, null when none — drives the UI. */
    var connectedName: String? by mutableStateOf(null)
        private set

    private var appContext: Context? = null
    private var inputManager: InputManager? = null

    /** An emulated button, in whichever session's own numbering is live. */
    private sealed interface EmuButton {
        data class Gb(val bit: Int) : EmuButton
        data class Psp(val button: Int) : EmuButton
        data class Mesen(val button: Int) : EmuButton
        data class Genesis(val button: Int) : EmuButton
    }

    private enum class PadDir { UP, DOWN, LEFT, RIGHT }
    private enum class Face { SOUTH, EAST, WEST, NORTH }

    /**
     * Which emulated button each currently-held physical input pressed.
     * Release uses this, not the current mapping, so a game switch or a
     * modifier change mid-press cannot leave a key stuck down.
     */
    private val active = mutableMapOf<String, EmuButton>()

    /** Physical inputs holding each emulated button down (D-pad and stick
     *  both drive directions). */
    private val holders = mutableMapOf<EmuButton, MutableSet<String>>()

    /**
     * Sources whose press a chord consumed. A consumed press never enters
     * [active], so without this set an analog source (the stick streams
     * motion events for as long as it is held) would re-fire its chord on
     * every event; iOS gets the edge for free from GameController's
     * value-changed handlers, Android has to keep it.
     */
    private val consumedSources = mutableSetOf<String>()

    /** Held modifier triggers, by source (a pad can report its trigger as
     *  a key AND an axis; the modifier stands while either reports held). */
    private val askHeld = mutableSetOf<String>()
    private val browseHeld = mutableSetOf<String>()

    private const val STICK_THRESHOLD = 0.5f

    // ---- lifecycle ------------------------------------------------------

    private val deviceListener = object : InputManager.InputDeviceListener {
        override fun onInputDeviceAdded(deviceId: Int) = refresh()
        override fun onInputDeviceRemoved(deviceId: Int) {
            // A removed pad can never send its releases: drop everything
            // it was holding (buttons, consumed chords, modifiers) even
            // when another controller stays connected.
            releaseDevice(deviceId)
            refresh()
        }
        override fun onInputDeviceChanged(deviceId: Int) = refresh()
    }

    /**
     * Begin watching for controllers. Safe in onCreate: this touches only
     * the platform InputManager — no speech bridge, no native library
     * (contrast MainActivity's warning about AccessibilityScript there).
     */
    fun start(context: Context) {
        appContext = context.applicationContext
        val manager = context.getSystemService(Context.INPUT_SERVICE) as? InputManager
        inputManager = manager
        manager?.registerInputDeviceListener(deviceListener, Handler(Looper.getMainLooper()))
        refresh()
    }

    fun stop() {
        inputManager?.unregisterInputDeviceListener(deviceListener)
        inputManager = null
        releaseAll()
        wasSessionActive = false
    }

    /** Re-scan: publish the first gamepad's name, or none. */
    private fun refresh() {
        // (getDeviceIds returns an IntArray, which has no mapNotNull —
        // map + filterNotNull it is.)
        val pads = InputDevice.getDeviceIds()
            .map { id -> InputDevice.getDevice(id) }
            .filterNotNull()
            .filter { it.isGameController() }
        val name = pads.firstOrNull()?.name?.takeIf { it.isNotBlank() } ?: pads.firstOrNull()?.let { "Game controller" }
        if (name == null && connectedName != null) releaseAll()
        connectedName = name
    }

    // ⛔ COMPLETE-MASK MATCHES, NEVER A NONZERO TEST. Android's source
    // constants share class bits (SOURCE_KEYBOARD is 0x101 and
    // SOURCE_GAMEPAD is 0x401 — they overlap in bit 0), so
    // `(sources and MASK) != 0` classifies an ordinary physical keyboard
    // as a controller: the pad would hide and its arrow keys would be
    // intercepted during play. Each allowed source must match in full.
    private fun InputDevice.isGameController(): Boolean =
        !isVirtual &&
            ((sources and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD ||
                (sources and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK)

    /** An event is ours when its device is a gamepad; a device-less event
     *  (some pads emit them) must at least carry a gamepad source flag. */
    private fun isGamepadEvent(device: InputDevice?, source: Int): Boolean =
        if (device != null) {
            device.isGameController()
        } else {
            (source and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD ||
                (source and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK ||
                (source and InputDevice.SOURCE_DPAD) == InputDevice.SOURCE_DPAD
        }

    /** A physical keyboard (the docked player's) is a play device too,
     *  but it is NOT a controller: [isGameController] drives
     *  [connectedName] and with it the on-screen pad, and a keyboard
     *  must set neither. Its test follows the pad test's rule — the
     *  complete source class or nothing (SOURCE_KEYBOARD is 0x101 and
     *  shares bit 0 with the gamepad class, so a nonzero test misfiles
     *  devices in both directions) — and the device must be real: the
     *  software keyboard is virtual, and its keys are the screen
     *  reader's business, never the game's. */
    private fun InputDevice.isKeyboard(): Boolean =
        !isVirtual &&
            (sources and InputDevice.SOURCE_KEYBOARD) == InputDevice.SOURCE_KEYBOARD

    /** A keyboard event is ours only when it arrives on a keyboard
     *  device. There is no device-less keyboard path: nearly every key
     *  event in Android claims the keyboard source class, so a source
     *  test alone would swallow keys that were never a keyboard's. */
    private fun isKeyboardEvent(device: InputDevice?): Boolean =
        device != null && device.isKeyboard()

    private val sessionActive: Boolean
        get() = GbGameSession.isRunning || PspGameSession.isRunning || MesenGameSession.isRunning || GenesisGameSession.isRunning

    /**
     * Session-boundary reset. Events are only routed while a session
     * runs, so a button or modifier still held when a game quits never
     * gets its release processed — the bookkeeping would survive into
     * the next session, dropping its first press or leaving a modifier
     * latched. Watching for the active flag to FLIP (either way) and
     * releasing everything at the boundary keeps the state honest
     * without this layer having to observe the sessions' internals.
     */
    private var wasSessionActive = false

    private fun syncSessionLifecycle() {
        val now = sessionActive
        if (now != wasSessionActive) {
            wasSessionActive = now
            releaseAll()
        }
    }

    /** Everything one device was holding, released: it is gone and can
     *  never send the releases itself. Source strings are built as
     *  "key:<deviceId>:<keycode>" and "motion:<deviceId>[.axis...]". */
    private fun releaseDevice(deviceId: Int) {
        fun owns(source: String): Boolean =
            source.startsWith("key:$deviceId:") ||
                source.startsWith("motion:$deviceId.") ||
                source == "motion:$deviceId"
        for (source in active.keys.filter(::owns)) {
            set(source, null, pressed = false)
        }
        consumedSources.removeAll(::owns)
        askHeld.removeAll(::owns)
        browseHeld.removeAll(::owns)
        shiftHeld.removeAll(::owns)
        if (shiftHeld.isEmpty()) shiftTapArmed = false
    }

    // ---- key events -------------------------------------------------------

    /**
     * One key event from the Activity funnel: a gamepad's, routed by
     * the when below, or a physical keyboard's, routed to
     * [handleKeyboardKeyEvent]. True when consumed.
     * Key repeats are consumed silently: the holder model below already
     * tracks the held state, and a chord must fire once per physical
     * press, not per repeat.
     */
    /** The keycodes the when below actually maps. Repeats of anything
     *  else (a pad's volume keys, say) must fall through unconsumed —
     *  swallowing them would break the key's long-press behaviour. */
    private val handledKeyCodes = setOf(
        KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_DOWN,
        KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_DPAD_RIGHT,
        KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_B,
        KeyEvent.KEYCODE_BUTTON_X, KeyEvent.KEYCODE_BUTTON_Y,
        KeyEvent.KEYCODE_BUTTON_L1, KeyEvent.KEYCODE_BUTTON_R1,
        KeyEvent.KEYCODE_BUTTON_L2, KeyEvent.KEYCODE_BUTTON_R2,
        KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_BUTTON_SELECT,
        KeyEvent.KEYCODE_BUTTON_THUMBL, KeyEvent.KEYCODE_BUTTON_THUMBR,
    )

    fun handleKeyEvent(event: KeyEvent): Boolean {
        syncSessionLifecycle()
        if (!sessionActive) return false
        if (!isGamepadEvent(event.device, event.source)) {
            // Not a pad event. A physical keyboard plays too (docked
            // play routes to its own handler); everything else — volume
            // keys, TalkBack's keys, the launcher's — keeps its path.
            if (isKeyboardEvent(event.device)) return handleKeyboardKeyEvent(event)
            return false
        }
        if (event.action == KeyEvent.ACTION_DOWN && event.repeatCount > 0) {
            return event.keyCode in handledKeyCodes
        }
        val pressed = event.action == KeyEvent.ACTION_DOWN
        val source = "key:${event.deviceId}:${event.keyCode}"
        return when (event.keyCode) {
            KeyEvent.KEYCODE_DPAD_UP -> { direction(PadDir.UP, source, pressed); true }
            KeyEvent.KEYCODE_DPAD_DOWN -> { direction(PadDir.DOWN, source, pressed); true }
            KeyEvent.KEYCODE_DPAD_LEFT -> { direction(PadDir.LEFT, source, pressed); true }
            KeyEvent.KEYCODE_DPAD_RIGHT -> { direction(PadDir.RIGHT, source, pressed); true }
            KeyEvent.KEYCODE_BUTTON_A -> { faceButton(Face.SOUTH, source, pressed); true }
            KeyEvent.KEYCODE_BUTTON_B -> { faceButton(Face.EAST, source, pressed); true }
            KeyEvent.KEYCODE_BUTTON_X -> { faceButton(Face.WEST, source, pressed); true }
            KeyEvent.KEYCODE_BUTTON_Y -> { faceButton(Face.NORTH, source, pressed); true }
            KeyEvent.KEYCODE_BUTTON_L1 -> { set(source, emuButton(GbKeys.L, PspKeys.L, MesenKeys.L, GenesisKeys.X), pressed); true }
            KeyEvent.KEYCODE_BUTTON_R1 -> { set(source, emuButton(GbKeys.R, PspKeys.R, MesenKeys.R, GenesisKeys.Z), pressed); true }
            KeyEvent.KEYCODE_BUTTON_START -> { set(source, emuButton(GbKeys.START, PspKeys.START, MesenKeys.START, GenesisKeys.START), pressed); true }
            KeyEvent.KEYCODE_BUTTON_SELECT -> { set(source, emuButton(GbKeys.SELECT, PspKeys.SELECT, MesenKeys.SELECT, GenesisKeys.MODE), pressed); true }
            // The reader's modifiers. Never game input.
            KeyEvent.KEYCODE_BUTTON_L2 -> { setModifier(askHeld, source, pressed); true }
            KeyEvent.KEYCODE_BUTTON_R2 -> { setModifier(browseHeld, source, pressed); true }
            // The stick clicks: the reader's two most-used commands, no
            // chord to learn first. Neither is a game button on any
            // console this app runs, so nothing is taken from the game.
            KeyEvent.KEYCODE_BUTTON_THUMBL -> { if (pressed) perform(Slot.WHERE_AM_I); true }
            KeyEvent.KEYCODE_BUTTON_THUMBR -> { if (pressed) perform(Slot.MENU_STATUS); true }
            else -> false
        }
    }

    /** The keycodes the keyboard when below actually maps — the pad's
     *  [handledKeyCodes] counterpart. Repeats of anything else (a
     *  keyboard's volume or brightness keys, say) fall through
     *  unconsumed, exactly as a pad's do. */
    private val keyboardKeyCodes = setOf(
        // Directions: the arrow keys.
        KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_DOWN,
        KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_DPAD_RIGHT,
        // The face diamond, by position: Z/X/A/S = south/east/west/north.
        KeyEvent.KEYCODE_Z, KeyEvent.KEYCODE_X,
        KeyEvent.KEYCODE_A, KeyEvent.KEYCODE_S,
        // Shoulders (Q/W) and Start (Enter). Select is Shift — it rides
        // its own row: a tap is Select, with J/L it changes category.
        KeyEvent.KEYCODE_Q, KeyEvent.KEYCODE_W,
        KeyEvent.KEYCODE_ENTER,
        KeyEvent.KEYCODE_SHIFT_LEFT, KeyEvent.KEYCODE_SHIFT_RIGHT,
        // The reader's item keys (J/K/L), path find (P) and outright
        // commands (Space/Tab).
        KeyEvent.KEYCODE_J, KeyEvent.KEYCODE_K,
        KeyEvent.KEYCODE_L, KeyEvent.KEYCODE_P,
        KeyEvent.KEYCODE_SPACE, KeyEvent.KEYCODE_TAB,
    )

    /**
     * One keyboard key event from the Activity funnel, while a session
     * runs. True when consumed. Game keys route through the pad's own
     * helpers — [direction], [faceButton], [set] — so a modifier held
     * on a connected pad chords a keyboard press exactly as it chords
     * a pad press (the held sets are shared); the reader's own keys
     * (J/K/L/P) perform their chord commands directly through
     * [readerKey]. The source strings keep the "key:<device>:<code>"
     * shape, so a release, a disconnect and the session-boundary reset
     * all treat a keyboard press like any other. Key repeats are
     * consumed silently, as on a pad.
     */
    private fun handleKeyboardKeyEvent(event: KeyEvent): Boolean {
        if (event.action == KeyEvent.ACTION_DOWN && event.repeatCount > 0) {
            return event.keyCode in keyboardKeyCodes
        }
        val pressed = event.action == KeyEvent.ACTION_DOWN
        val source = "key:${event.deviceId}:${event.keyCode}"
        return when (event.keyCode) {
            KeyEvent.KEYCODE_DPAD_UP -> { direction(PadDir.UP, source, pressed); true }
            KeyEvent.KEYCODE_DPAD_DOWN -> { direction(PadDir.DOWN, source, pressed); true }
            KeyEvent.KEYCODE_DPAD_LEFT -> { direction(PadDir.LEFT, source, pressed); true }
            KeyEvent.KEYCODE_DPAD_RIGHT -> { direction(PadDir.RIGHT, source, pressed); true }
            KeyEvent.KEYCODE_Z -> { faceButton(Face.SOUTH, source, pressed); true }
            KeyEvent.KEYCODE_X -> { faceButton(Face.EAST, source, pressed); true }
            KeyEvent.KEYCODE_A -> { faceButton(Face.WEST, source, pressed); true }
            KeyEvent.KEYCODE_S -> { faceButton(Face.NORTH, source, pressed); true }
            KeyEvent.KEYCODE_Q -> { set(source, emuButton(GbKeys.L, PspKeys.L, MesenKeys.L, GenesisKeys.X), pressed); true }
            KeyEvent.KEYCODE_W -> { set(source, emuButton(GbKeys.R, PspKeys.R, MesenKeys.R, GenesisKeys.Z), pressed); true }
            KeyEvent.KEYCODE_ENTER -> { set(source, emuButton(GbKeys.START, PspKeys.START, MesenKeys.START, GenesisKeys.START), pressed); true }
            // Shift is Select AND the item keys' category modifier;
            // shiftKey decides which of the two the press was.
            KeyEvent.KEYCODE_SHIFT_LEFT, KeyEvent.KEYCODE_SHIFT_RIGHT -> { shiftKey(source, pressed); true }
            // The reader's item keys and path find: the pad's chord
            // commands, performed directly (see readerKey).
            KeyEvent.KEYCODE_J -> { itemKey(previous = true, event, source, pressed); true }
            KeyEvent.KEYCODE_L -> { itemKey(previous = false, event, source, pressed); true }
            KeyEvent.KEYCODE_K -> { readerKey(Input.FaceBtn(Face.SOUTH), ask = false, source, pressed); true }
            KeyEvent.KEYCODE_P -> { readerKey(Input.FaceBtn(Face.WEST), ask = true, source, pressed); true }
            // The stick clicks' commands on the keyboard's own keys:
            // press-only, like the clicks.
            KeyEvent.KEYCODE_SPACE -> { if (pressed) perform(Slot.WHERE_AM_I); true }
            KeyEvent.KEYCODE_TAB -> { if (pressed) perform(Slot.MENU_STATUS); true }
            else -> false
        }
    }

    /** Shift sources currently down, and whether the press is still a
     *  tap candidate: Shift is Select when tapped alone, and the item
     *  keys' category modifier while held — see [shiftKey]. */
    private val shiftHeld = mutableSetOf<String>()
    private var shiftTapArmed = false

    /**
     * Perform the reader command [input] means under one modifier —
     * the command the pad reaches by holding the modifier and pressing.
     * The hold is registered for the routing call only: [chord] reads
     * the held sets, so the key means what the pad's chord means BY
     * CONSTRUCTION, not by a second copy of the table. The release side
     * never consults the modifiers (see [routeChord]), so removing the
     * hold when the call returns cannot strand anything.
     */
    private fun readerKey(input: Input, ask: Boolean, source: String, pressed: Boolean): Boolean {
        val held = if (ask) askHeld else browseHeld
        held.add(source)
        val consumed = routeChord(input, source, pressed)
        held.remove(source)
        return consumed
    }

    /**
     * J and L: previous / next item — or, with Shift held, previous /
     * next category (the pad's browse groups). Using Shift this way
     * spends its press: the tap no longer means Select.
     */
    private fun itemKey(previous: Boolean, event: KeyEvent, source: String, pressed: Boolean) {
        if (pressed && event.isShiftPressed) {
            shiftTapArmed = false
            readerKey(Input.Pad(if (previous) PadDir.UP else PadDir.DOWN), ask = false, source, pressed)
        } else {
            readerKey(Input.Pad(if (previous) PadDir.LEFT else PadDir.RIGHT), ask = false, source, pressed)
        }
    }

    /**
     * Shift's two jobs. As a modifier it must work while held; as
     * Select it cannot fire on the way down — every Shift+J would also
     * tap Select in the game. So a press dispatches nothing: if J or L
     * uses it, the press is spent; if the last Shift comes back up
     * unused, Select fires then, pressed and released at once. Select
     * is a tap in every game this app runs; nothing holds it.
     */
    private fun shiftKey(source: String, pressed: Boolean): Boolean {
        if (pressed) {
            if (shiftHeld.isEmpty()) shiftTapArmed = true
            shiftHeld.add(source)
            return true
        }
        shiftHeld.remove(source)
        if (shiftHeld.isEmpty() && shiftTapArmed) {
            shiftTapArmed = false
            val button = emuButton(GbKeys.SELECT, PspKeys.SELECT, MesenKeys.SELECT, GenesisKeys.MODE)
            set(source, button, true)
            set(source, button, false)
        }
        return true
    }

    private fun setModifier(held: MutableSet<String>, source: String, pressed: Boolean) {
        if (pressed) held.add(source) else held.remove(source)
    }

    // ---- motion events (left stick + D-pad hat) ---------------------------

    /**
     * One gamepad motion event: the left stick as a second D-pad (0.5
     * deadzone, iOS's threshold, so drift never walks) and the D-pad hat
     * some pads report instead of key events. Android's Y axis points
     * down (up is negative) — the opposite of GameController's — so the
     * comparisons are flipped relative to the Swift.
     */
    fun handleMotionEvent(event: MotionEvent): Boolean {
        syncSessionLifecycle()
        if (!sessionActive) return false
        if (!isGamepadEvent(event.device, event.source)) return false
        val id = "motion:${event.deviceId}"
        // ⛔ MODIFIERS BEFORE DIRECTIONS. An axes-only pad can report a
        // trigger and a direction in the SAME event; routing the
        // direction first would read the modifier state of the previous
        // event — sending game input where a chord was meant, or firing
        // a stale chord on a release-plus-direction event. Update both
        // trigger modifiers before any direction is routed.
        // Analog triggers are the same modifiers as BUTTON_L2/R2, for
        // pads that report them only as axes. iOS reads GameController's
        // pressed flag (its own internal threshold); Android hands over
        // the raw value, so the stick's 0.5 stands in for it. Some pads
        // name the pair BRAKE/GAS instead of LTRIGGER/RTRIGGER — take
        // whichever reports further.
        val l2 = maxOf(
            event.getAxisValue(MotionEvent.AXIS_LTRIGGER),
            event.getAxisValue(MotionEvent.AXIS_BRAKE),
        )
        setModifier(askHeld, "$id.axis.l2", l2 > STICK_THRESHOLD)
        val r2 = maxOf(
            event.getAxisValue(MotionEvent.AXIS_RTRIGGER),
            event.getAxisValue(MotionEvent.AXIS_GAS),
        )
        setModifier(browseHeld, "$id.axis.r2", r2 > STICK_THRESHOLD)
        val x = event.getAxisValue(MotionEvent.AXIS_X)
        val y = event.getAxisValue(MotionEvent.AXIS_Y)
        direction(PadDir.UP, "$id.stick.up", y < -STICK_THRESHOLD)
        direction(PadDir.DOWN, "$id.stick.down", y > STICK_THRESHOLD)
        direction(PadDir.LEFT, "$id.stick.left", x < -STICK_THRESHOLD)
        direction(PadDir.RIGHT, "$id.stick.right", x > STICK_THRESHOLD)
        val hatX = event.getAxisValue(MotionEvent.AXIS_HAT_X)
        val hatY = event.getAxisValue(MotionEvent.AXIS_HAT_Y)
        direction(PadDir.UP, "$id.hat.up", hatY < -STICK_THRESHOLD)
        direction(PadDir.DOWN, "$id.hat.down", hatY > STICK_THRESHOLD)
        direction(PadDir.LEFT, "$id.hat.left", hatX < -STICK_THRESHOLD)
        direction(PadDir.RIGHT, "$id.hat.right", hatX > STICK_THRESHOLD)
        return true
    }

    // ---- routing ------------------------------------------------------------

    private fun direction(dir: PadDir, source: String, pressed: Boolean) {
        if (routeChord(Input.Pad(dir), source, pressed)) return
        val button = when (dir) {
            PadDir.UP -> emuButton(GbKeys.UP, PspKeys.UP, MesenKeys.UP, GenesisKeys.UP)
            PadDir.DOWN -> emuButton(GbKeys.DOWN, PspKeys.DOWN, MesenKeys.DOWN, GenesisKeys.DOWN)
            PadDir.LEFT -> emuButton(GbKeys.LEFT, PspKeys.LEFT, MesenKeys.LEFT, GenesisKeys.LEFT)
            PadDir.RIGHT -> emuButton(GbKeys.RIGHT, PspKeys.RIGHT, MesenKeys.RIGHT, GenesisKeys.RIGHT)
        }
        set(source, button, pressed)
    }

    private fun faceButton(face: Face, source: String, pressed: Boolean) {
        if (routeChord(Input.FaceBtn(face), source, pressed)) return
        set(source, if (pressed) gameFaceButton(face) else null, pressed)
    }

    /**
     * The chord gate both routing paths pass through first, in iOS's
     * shape: on a FRESH press with a modifier held, an assigned
     * combination performs its reader command — consumed: the game never
     * sees the press — while an unassigned one falls through to game
     * input (L2 + D-pad left still moves, rather than doing nothing).
     * True when this press (or its release) was consumed here.
     */
    private fun routeChord(input: Input, source: String, pressed: Boolean): Boolean {
        if (!pressed) return consumedSources.remove(source)
        if (source in consumedSources) return true
        if (active.containsKey(source)) return false
        val slot = chord(input) ?: return false
        consumedSources.add(source)
        perform(slot)
        return true
    }

    /** The emulated button for a position, in the running session's terms. */
    private fun emuButton(gb: Int, psp: Int, mesen: Int, genesis: Int): EmuButton? = when {
        PspGameSession.isRunning -> EmuButton.Psp(psp)
        MesenGameSession.isRunning -> EmuButton.Mesen(mesen)
        GenesisGameSession.isRunning -> EmuButton.Genesis(genesis)
        GbGameSession.isRunning -> EmuButton.Gb(gb)
        else -> null
    }

    /**
     * Maps a physical face position to the emulated console's button.
     * PSP names its four the Sony way, in the same physical positions;
     * the Game Boy follows the Nintendo order (A east, B south) and has
     * two face buttons — north and west map to nothing, as on iOS.
     */
    private fun gameFaceButton(face: Face): EmuButton? {
        if (MesenGameSession.isRunning) {
            // The Nintendo order, like the Game Boy's — but the SNES
            // pad has all four positions (A east, B south, X north, Y
            // west). On a NES game the core drops the X/Y values the
            // console does not have, so one mapping serves both.
            return EmuButton.Mesen(
                when (face) {
                    Face.EAST -> MesenKeys.A
                    Face.SOUTH -> MesenKeys.B
                    Face.NORTH -> MesenKeys.X
                    Face.WEST -> MesenKeys.Y
                }
            )
        }
        if (PspGameSession.isRunning) {
            return EmuButton.Psp(
                when (face) {
                    Face.SOUTH -> PspKeys.CROSS
                    Face.EAST -> PspKeys.CIRCLE
                    Face.NORTH -> PspKeys.TRIANGLE
                    Face.WEST -> PspKeys.SQUARE
                }
            )
        }
        if (GenesisGameSession.isRunning) {
            // The Genesis pad's own layout, by position: its bottom
            // row is A (west), B (south), C (east), and the 6-button
            // pad's Y sits at north — the slots the shared registry's
            // Genesis table assigns (GenesisKeys), with L1/R1
            // carrying X/Z (see the key routing above).
            return EmuButton.Genesis(
                when (face) {
                    Face.WEST -> GenesisKeys.A
                    Face.SOUTH -> GenesisKeys.B
                    Face.EAST -> GenesisKeys.C
                    Face.NORTH -> GenesisKeys.Y
                }
            )
        }
        if (GbGameSession.isRunning) {
            return when (face) {
                Face.EAST -> EmuButton.Gb(GbKeys.A)
                Face.SOUTH -> EmuButton.Gb(GbKeys.B)
                Face.NORTH, Face.WEST -> null
            }
        }
        return null
    }

    /**
     * Edge-only: an emulated button goes down when its first holder
     * presses and up when its last holder releases. The release half
     * ignores the passed button and uses [active], exactly as iOS does.
     */
    private fun set(source: String, button: EmuButton?, pressed: Boolean) {
        if (pressed) {
            if (active.containsKey(source) || button == null) return
            active[source] = button
            val wasDown = holders[button]?.isNotEmpty() == true
            holders.getOrPut(button) { mutableSetOf() }.add(source)
            if (!wasDown) dispatch(button, down = true)
        } else {
            val held = active.remove(source) ?: return
            holders[held]?.remove(source)
            if (holders[held]?.isEmpty() != false) dispatch(held, down = false)
        }
    }

    private fun dispatch(button: EmuButton, down: Boolean) {
        when (button) {
            is EmuButton.Gb ->
                if (down) GbGameSession.pressKey(button.bit) else GbGameSession.releaseKey(button.bit)
            is EmuButton.Psp ->
                if (down) PspGameSession.pressButton(button.button) else PspGameSession.releaseButton(button.button)
            is EmuButton.Mesen ->
                if (down) MesenGameSession.pressButton(button.button) else MesenGameSession.releaseButton(button.button)
            is EmuButton.Genesis ->
                if (down) GenesisGameSession.pressButton(button.button) else GenesisGameSession.releaseButton(button.button)
        }
    }

    private fun releaseAll() {
        for ((button, sources) in holders) {
            if (sources.isNotEmpty()) dispatch(button, down = false)
        }
        holders.clear()
        active.clear()
        consumedSources.clear()
        askHeld.clear()
        browseHeld.clear()
        shiftHeld.clear()
        shiftTapArmed = false
    }

    // ---- reader chords ------------------------------------------------------

    private sealed interface Input {
        data class Pad(val dir: PadDir) : Input
        data class FaceBtn(val face: Face) : Input
    }

    /** The reader's command slots — what a chord means, before it is
     *  resolved against the loaded game. */
    private enum class Slot {
        WHERE_AM_I, MENU_STATUS, STOP_SPEECH, REPEAT_LAST, FIND_PATH, TILES,
        PREV_ITEM, NEXT_ITEM, PREV_GROUP, NEXT_GROUP, READ_ITEM,
        GAME_TOGGLE_1, GAME_TOGGLE_2, GAME_TOGGLE_3,
        /** One line further back through the spoken history — separate
         *  from REPEAT_LAST because the walk has two ends. */
        REPEAT_OLDER,
    }

    /** Null when no modifier is held (the press is game input), or for an
     *  unassigned combination (also passed through). */
    private fun chord(input: Input): Slot? {
        if (askHeld.isNotEmpty()) {
            return when (input) {
                Input.FaceBtn(Face.SOUTH) -> Slot.WHERE_AM_I
                Input.FaceBtn(Face.EAST) -> Slot.STOP_SPEECH
                Input.FaceBtn(Face.NORTH) -> Slot.REPEAT_LAST
                Input.FaceBtn(Face.WEST) -> Slot.FIND_PATH
                Input.Pad(PadDir.DOWN) -> Slot.TILES
                Input.Pad(PadDir.UP) -> Slot.REPEAT_OLDER
                else -> null
            }
        }
        if (browseHeld.isNotEmpty()) {
            return when (input) {
                Input.Pad(PadDir.LEFT) -> Slot.PREV_ITEM
                Input.Pad(PadDir.RIGHT) -> Slot.NEXT_ITEM
                Input.Pad(PadDir.UP) -> Slot.PREV_GROUP
                Input.Pad(PadDir.DOWN) -> Slot.NEXT_GROUP
                Input.FaceBtn(Face.SOUTH) -> Slot.READ_ITEM
                Input.FaceBtn(Face.NORTH) -> Slot.GAME_TOGGLE_1
                Input.FaceBtn(Face.WEST) -> Slot.GAME_TOGGLE_2
                Input.FaceBtn(Face.EAST) -> Slot.GAME_TOGGLE_3
                else -> null
            }
        }
        return null
    }

    private sealed interface Action {
        data class Adapter(val command: Int) : Action
        data class Hotkey(val key: Char) : Action
        object StopSpeech : Action
        object RepeatNewest : Action
        object RepeatOlder : Action
    }

    /**
     * Native reader first, then the Pokémon script key — the same buttons
     * the on-screen reader groups offer for this game, and only those.
     *
     * The sets are the platforms' own: for PSP, the running adapter's
     * supported commands (AdapterLibraryData, generated from the same
     * registry table iOS's AdapterCommand.supported reads — so, e.g.,
     * Menu status resolves on MKU/KHBBS but not on Dissidia, exactly as
     * on iOS). For Game Boy, the script keys of iOS's GameSystem GB
     * tables: path P/M/E, reading K/J/L. The GB reader has NO speech keys
     * (R moves its camera; U is unbound) and no I/O group keys (those are
     * shift-chords a single tap cannot send), so Stop speech falls to the
     * engine stop and Repeat to the bridge's last line, and the group
     * chords resolve to nothing — the script's absences, not omissions.
     */
    private fun resolve(slot: Slot): Action? {
        val native: Set<Int> = if (PspGameSession.isRunning) {
            AdapterLibraryData.games.firstOrNull { it.id == PspGameSession.adapterId }
                ?.commands?.toSet() ?: emptySet()
        } else if (MesenGameSession.isRunning) {
            // The Mesen consoles' native reader (A Link to the Past):
            // its supported set from the same generated table. The
            // command values compared below are the shared ABI's, and
            // MesenAdapterCommands mirrors them for this session.
            AdapterLibraryData.games.firstOrNull { it.id == MesenGameSession.adapterId }
                ?.commands?.toSet() ?: emptySet()
        } else {
            emptySet()
        }
        val luaKeys: Set<Char> = if (GbGameSession.isRunning) {
            setOf('P', 'M', 'E', 'K', 'J', 'L')
        } else {
            emptySet()
        }
        fun cmd(command: Int): Action? = if (command in native) Action.Adapter(command) else null
        fun key(key: Char): Action? = if (key in luaKeys) Action.Hotkey(key) else null
        return when (slot) {
            Slot.WHERE_AM_I -> cmd(PspAdapterCommands.WHERE_AM_I) ?: key('C') ?: key('M')
            Slot.MENU_STATUS -> cmd(PspAdapterCommands.MENU_STATE)
            // The DS script's own stop key also clears its queue; elsewhere
            // the engine stop is what makes silence stick. (No DS session
            // exists on Android yet, so the key half never fires here —
            // the order is kept so the port stays honest when one lands.)
            Slot.STOP_SPEECH -> key('R') ?: Action.StopSpeech
            // ⛔ NOT gated on an adapter: the spoken history exists for
            // every game. The script's own U key takes precedence where
            // it exists (DS only — not on GB, per the set above).
            Slot.REPEAT_LAST -> key('U') ?: Action.RepeatNewest
            Slot.FIND_PATH -> key('P')
            Slot.TILES -> key('E')
            Slot.PREV_ITEM -> cmd(PspAdapterCommands.PREV_ALLY) ?: key('J')
            Slot.NEXT_ITEM -> cmd(PspAdapterCommands.NEXT_ALLY) ?: key('L')
            Slot.PREV_GROUP -> cmd(PspAdapterCommands.PREV_ENEMY) ?: key('I')
            Slot.NEXT_GROUP -> cmd(PspAdapterCommands.NEXT_ENEMY) ?: key('O')
            Slot.READ_ITEM -> cmd(PspAdapterCommands.NEXT_UNACTED_ALLY) ?: key('K')
            Slot.GAME_TOGGLE_1 -> cmd(PspAdapterCommands.CUST_TOGGLE)
            Slot.GAME_TOGGLE_2 -> cmd(PspAdapterCommands.CHAR_TOGGLE)
            Slot.GAME_TOGGLE_3 -> cmd(PspAdapterCommands.OSK_TOGGLE)
            // The walk's other end: no script key exists for it anywhere,
            // so it is the platform answer directly.
            Slot.REPEAT_OLDER -> Action.RepeatOlder
        }
    }

    private fun perform(slot: Slot) {
        val action = resolve(slot)
        if (action == null) {
            // A chord with nothing behind it must say so: silence reads
            // as a broken controller. (iOS's line, verbatim.)
            announce("No reader command there for this game.")
            return
        }
        when (action) {
            is Action.Adapter -> {
                if (MesenGameSession.isRunning) {
                    if (!MesenGameSession.sendAdapterCommand(action.command)) {
                        announce(
                            if (MesenGameSession.adapterName.isNotEmpty()) {
                                "Not ready yet."
                            } else {
                                "No reader."
                            }
                        )
                    }
                } else if (!PspGameSession.sendAdapterCommand(action.command)) {
                    // A supported command the adapter refused (not ready
                    // yet): the launcher screen's own refusal lines for
                    // the same situation (PspReaderGroup's contract).
                    announce(
                        if (PspGameSession.adapterName.isNotEmpty()) {
                            "Not ready yet."
                        } else {
                            "No reader."
                        }
                    )
                }
            }
            is Action.Hotkey -> GbGameSession.tapHotkey(action.key)
            Action.StopSpeech -> {
                val context = appContext
                if (PspGameSession.isRunning && context != null) {
                    PspGameSession.stopSpeech(context)
                } else if (MesenGameSession.isRunning && context != null) {
                    MesenGameSession.stopSpeech(context)
                } else if (GenesisGameSession.isRunning && context != null) {
                    // The bridge stop is the whole of it (no reader
                    // queue behind it — see GenesisGameSession).
                    GenesisGameSession.stopSpeech(context)
                } else {
                    GbGameSession.stopSpeech()
                }
            }
            Action.RepeatNewest -> {
                // The bridge re-speaks its own last line. Two honest
                // deltas from iOS, both in the bridge rather than here:
                // it keeps only the last line (no history to walk), and
                // it stays silent when nothing has been spoken, where
                // iOS says "Nothing to repeat." A session speaks a
                // startup line before any chord can be pressed, so the
                // silent case needs a chord before the game's first
                // line — narrow, and recorded in the PR.
                val context = appContext ?: return
                try {
                    AccessibilityScript.speechBridge(context).repeatLast()
                } catch (t: Throwable) {
                    // No bridge exists; nothing to repeat.
                }
            }
            Action.RepeatOlder -> {
                // Android has no spoken-history walk yet (the bridge
                // keeps only its last line — the walk is Stage 3's
                // wiring), so the chord says iOS's own walk-exhausted
                // line rather than going silent.
                announce("Nothing older to repeat.")
            }
        }
    }

    /**
     * A line from the controller layer, through the shared speech bridge
     * — the same voice the reader uses, queued behind it rather than
     * interrupting. Only reached while a session runs, which is when the
     * bridge exists (see MainActivity's warning); speak() itself
     * degrades safely if it somehow does not.
     */
    private fun announce(text: String) {
        AccessibilityScript.speak(text, false)
    }
}

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

/**
 * Physical game controllers (Xbox, PlayStation, generic Bluetooth/USB
 * pads): the console's buttons, plus — from the next commit — the
 * reader's commands as trigger chords.
 *
 * ⛔ THIS IS A PORT OF iOS's ControllerInput.swift, BEHAVIOUR FIRST. Game
 * presses go through the sessions' existing key plumbing
 * (GbGameSession.pressKey / PspGameSession.pressButton — the same calls
 * the on-screen pad makes), so the reader's echoes (tracked menus, the
 * PSP OSK) see a controller press exactly as they see a tap. There is no
 * parallel input system. While a controller is connected the on-screen
 * game pad is hidden (the launcher reads [connectedName]).
 *
 * Face buttons map BY POSITION to the emulated console's layout, not by
 * the letter printed on the controller: on the Game Boy the right button
 * is A and the bottom one B (Nintendo's layout); on the PSP the four
 * positions are Cross/Circle/Triangle/Square exactly as on a PlayStation
 * controller. Android's standard gamepad keycodes are already positional
 * (BUTTON_A is the bottom button, BUTTON_B the right one, whatever the
 * pad's brand), so the position mapping survives the platform change.
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

    private const val STICK_THRESHOLD = 0.5f

    // ---- lifecycle ------------------------------------------------------

    private val deviceListener = object : InputManager.InputDeviceListener {
        override fun onInputDeviceAdded(deviceId: Int) = refresh()
        override fun onInputDeviceRemoved(deviceId: Int) = refresh()
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
    }

    /** Re-scan: publish the first gamepad's name, or none. */
    private fun refresh() {
        val pads = InputDevice.getDeviceIds()
            .mapNotNull { InputDevice.getDevice(it) }
            .filter { it.isGameController() }
        val name = pads.firstOrNull()?.name?.takeIf { it.isNotBlank() } ?: pads.firstOrNull()?.let { "Game controller" }
        if (name == null && connectedName != null) releaseAll()
        connectedName = name
    }

    private fun InputDevice.isGameController(): Boolean =
        !isVirtual &&
            (sources and (InputDevice.SOURCE_GAMEPAD or InputDevice.SOURCE_JOYSTICK)) != 0

    /** An event is ours when its device is a gamepad; a device-less event
     *  (some pads emit them) must at least carry a gamepad source flag. */
    private fun isGamepadEvent(device: InputDevice?, source: Int): Boolean =
        if (device != null) {
            device.isGameController()
        } else {
            (source and (InputDevice.SOURCE_GAMEPAD or InputDevice.SOURCE_JOYSTICK or InputDevice.SOURCE_DPAD)) != 0
        }

    private val sessionActive: Boolean
        get() = GbGameSession.isRunning || PspGameSession.isRunning

    // ---- key events -------------------------------------------------------

    /**
     * One gamepad key event from the Activity funnel. True when consumed.
     * Key repeats are consumed silently: the holder model below already
     * tracks the held state, and a chord (next commit) must fire once per
     * physical press, not per repeat.
     */
    fun handleKeyEvent(event: KeyEvent): Boolean {
        if (!sessionActive) return false
        if (!isGamepadEvent(event.device, event.source)) return false
        if (event.action == KeyEvent.ACTION_DOWN && event.repeatCount > 0) return true
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
            KeyEvent.KEYCODE_BUTTON_L1 -> { set(source, emuButton(GbKeys.L, PspKeys.L), pressed); true }
            KeyEvent.KEYCODE_BUTTON_R1 -> { set(source, emuButton(GbKeys.R, PspKeys.R), pressed); true }
            KeyEvent.KEYCODE_BUTTON_START -> { set(source, emuButton(GbKeys.START, PspKeys.START), pressed); true }
            KeyEvent.KEYCODE_BUTTON_SELECT -> { set(source, emuButton(GbKeys.SELECT, PspKeys.SELECT), pressed); true }
            else -> false
        }
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
        if (!sessionActive) return false
        if (!isGamepadEvent(event.device, event.source)) return false
        val id = "motion:${event.deviceId}"
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
        val button = when (dir) {
            PadDir.UP -> emuButton(GbKeys.UP, PspKeys.UP)
            PadDir.DOWN -> emuButton(GbKeys.DOWN, PspKeys.DOWN)
            PadDir.LEFT -> emuButton(GbKeys.LEFT, PspKeys.LEFT)
            PadDir.RIGHT -> emuButton(GbKeys.RIGHT, PspKeys.RIGHT)
        }
        set(source, button, pressed)
    }

    private fun faceButton(face: Face, source: String, pressed: Boolean) {
        set(source, if (pressed) gameFaceButton(face) else null, pressed)
    }

    /** The emulated button for a position, in the running session's terms. */
    private fun emuButton(gb: Int, psp: Int): EmuButton? = when {
        PspGameSession.isRunning -> EmuButton.Psp(psp)
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
        }
    }

    private fun releaseAll() {
        for ((button, sources) in holders) {
            if (sources.isNotEmpty()) dispatch(button, down = false)
        }
        holders.clear()
        active.clear()
    }
}

# Wave 20 - the MIDI input foundation

**Status:** shipped in v1.23.0 (phases A and B).
**Kanso:** card 244 (S - MIDI input foundation).
**Goal:** the app listens to a MIDI input. Pick the device, see what
arrives, and bind a control to a parameter. The MIDI scrub transport
(UPIC-28) is the wave that *uses* this; this one is only the plumbing.

## Why

Everything the score does is a function of time, so the next big feature -
a signed playhead driven by MIDI - needs three things the app has never
had: a MIDI input, a way to see that messages arrive, and a way to bind a
control to something. Building them now, with two ordinary parameters as
the proof, means the scrub wave can concentrate on the transport instead
of inventing a controller layer at the same time.

The reverse half of UPIC-22 is already shipped (the sample reverse in
v1.19, the live reverse in v1.20), so the MIDI controller is the last
missing piece of the original idea.

## What it looks like

The **Audio Settings** overlay gets a MIDI block above the audio device
row:

```
MIDI in        [ None | <the system's MIDI inputs> ]
CC 7 = 100   ch 1   <device name>
Master gain    [ Learn ]   CC 7 ch 1
Intensity      [ Learn ]   note 60 ch 1
```

- **the device combo** lists `juce::MidiInput::getAvailableDevices()`
  (`None` when there is no device). The list is rebuilt every time the
  overlay opens, so a controller plugged in after the app started shows
  up; the open device stays selected (and is closed if it disappeared).
- **the activity line** shows the last message: CC / note on / note off,
  the number, the value, the channel and the device.
- **one row per bindable parameter** with a Learn button and the current
  binding. While that row is learning, the button reads **Cancel**.

## The decisions

### 1. The MIDI thread only publishes; the message thread decides

JUCE calls the input callback on a high-priority system thread. The
callback does one thing: pack the message (type, channel, number, value)
into the low 32 bits of a `std::atomic<uint64_t>` together with a counter
in the high 32 bits, in a **single** store. One store, one load - the
reader always sees a consistent pair.

The 30 Hz status timer polls that word and does the learn, the binding
and the parameter application on the message thread. No locks, no
`callAsync` per message (a MIDI flood would flood the message queue), and
no UI call from the MIDI thread.

### 2. Session state, not piece state

A MIDI binding is a *controller map* - this CC is my knob on this machine.
Like the pitch-mode dropdowns and the arc list visibility it is not part
of the score, so it is not in the `.upic.json` format, and it is not
saved across launches. The future-ideas write-up already said the MIDI
slice "does not touch the score model, the file format or the UI model".

### 3. The learn takes a CC or a note, nothing else

Pressing Learn arms the row; the next **CC** or **note on** binds to it.
The channel is recorded too, so two controllers can share a CC number on
different channels. Real-time messages (clock, active sensing) and other
channel messages (pitch bend, aftertouch) are **ignored** - a clock must
not steal the learn - and the button cancels instead.

### 4. Two targets, to prove the path

**Master gain** (the audio path) and **draw intensity** (the drawing
path) are enough to prove the mechanism end to end. The scrub wave adds
its own target, which is an *action* rather than a parameter: a
different kind of binding on the same table.

### 5. The MIDI wins while a parameter is bound

A bound parameter follows the incoming control, and the slider is moved
too so the UI shows the value. Moving the slider afterwards is overridden
by the next message; Clear hands the parameter back to the UI. A CC's
0..127 maps to 0..1, so the master gain's 1.5 headroom stays a UI-only
range - documented here rather than treated as a bug.

## The risks that were real

- **A race between the MIDI thread and the UI.** The single packed
  atomic removes it.
- **A flood of messages.** Nothing is posted per message; the timer reads
  the newest one, so a stuck controller costs nothing.
- **The device list at startup.** Fixed by rebuilding it when the
  overlay opens.
- **A parameter fighting the UI.** Resolved by "the MIDI wins while
  bound", with Clear as the way out.

## Tests

The selftest runs headless (no MIDI device), so the hub has a test hook,
`injectForTest()`, which writes the same atomic word the device callback
writes. Nine new checks (243 -> 252):

- the device combo is present and starts at `None`;
- a CC binds to the master gain, and the gain follows (100/127);
- the activity line shows the message;
- a note binds to the draw intensity (64/127);
- a clock message does not cancel the learn;
- the learn can be cancelled;
- Clear leaves the parameter alone afterwards.

Gate: app 252/252, plugin smoke 15/15, pluginval strictness 5 SUCCESS.

## What came out of it

- The app is a MIDI listener; the next wave can be a transport.
- The learn is deliberately generic - a target, a match and a value -
  so the scrub's action binding plugs into the same table.
- `MidiHub.h` is its own header (like `ArcListPanel.h`), shared by the
  app and the plugin with no new build wiring; the `.jucer` lists it.
- **Not covered:** a real controller (the device list and the callback
  are JUCE's; the standing device test item applies), and the VST3
  wrapper does not declare a MIDI input yet - that belongs to the scrub
  wave, where the host's MIDI becomes the second way in.

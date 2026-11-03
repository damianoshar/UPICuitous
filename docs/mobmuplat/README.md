# MobMuPlat scrub controller

1. Copy `UPICuitous-Scrub.mmp` and `UPICuitous-Scrub.pd` to the phone
   (MobMuPlat's Documents folder).
2. Load `UPICuitous-Scrub.mmp` in MobMuPlat.
3. Open MobMuPlat's Network settings and set the output address to the
   computer's LAN IP and the output port to `9000`.

   (If the settings cannot be saved, leave the defaults: UPICuitous also
   listens on MobMuPlat's default multicast output `224.0.0.1:54321`.)
4. Start UPICuitous. The status line shows `OSC 9000` while the receiver
   is listening.
5. Drag the slider. `0` is the page start and `1` is the page end. The
   value feeds the same scrub preview as a canvas drag; releasing never
   starts playback. MobMuPlat only sends a value when the slider moves,
   so the app ends the gesture 2000 ms after the last change; holding
   still freezes the scrub preview at the last position.

The Pd patch forwards the slider's `/upic/scrub` value to MobMuPlat's
`toNetwork` output. If the phone cannot reach the computer, check the
Windows firewall for UDP port 9000 and make sure both devices are on the
same WiFi network.

## Tilt patch

`UPICuitous-Tilt.mmp` turns the phone into a tilt controller. It only
sends tilt values while that patch is loaded (the app enables tilt for
filenames containing `tilt`). Copy `UPICuitous-Tilt.mmp` and
`UPICuitous-Tilt.pd` to the phone, load the `.mmp`, then tap the toggle
to enable output.

Hold the phone roughly level and tilt left/right. The gain is fixed, so
the full page is reachable at about 40 degrees. The slider sets
smoothing:

- left = raw and quick, but hand tremor can be audible
- right = stable, but the playhead follows with a delay

The Tilt input meter shows the smoothed value that drives UPICuitous.
Holding the phone still freezes the scrub preview; tilting again
resumes it.

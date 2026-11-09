# Bundled ClickEncoder

Source: [0xPIT/encoder, revision d6d5738fdf](https://github.com/0xPIT/encoder/tree/d6d5738fdf),
previously installed as `0xpit/ClickEncoder@0.0.0-alpha+sha.d6d5738fdf`.
The original headers and [BSD license](LICENSING.txt) are retained.

Local changes:

- Compile the implementation only when `PSU_ENABLE_ENCODER` is enabled.
- Decrement `doubleClickTicks` once per button service interval. Upstream
  decrements it twice, so the value 1 used by single-click mode underflows
  to 255 and never produces `Clicked`. That prevented the wheel from
  acknowledging alerts and selecting UI actions.

`WheelInput` includes this copy directly. Both PlatformIO and Arduino IDE build
it from `src/`; an external ClickEncoder installation is not required. TimerOne
still samples at 1 ms. The existing rotation, hold and fresh-gesture guard remain
in use.

`python3 tools/test_wheel_alert_guard.py` compiles this driver and the real wheel
adapter against simulated GPIO and the registered timer callback. It covers
short/repeated clicks, hold/release, both rotation directions and stale-gesture
suppression across alert popups. The previous test supplied synthetic Clicked
events and could not detect this driver bug.

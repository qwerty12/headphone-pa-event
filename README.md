# headphone-pa-event
Perform hardcoded actions when headphones are (un)plugged

This:
* always mutes the speaker sound when headphones are removed
* starts [PulseEffects](https://github.com/wwmm/pulseeffects) with the Boosted preset from [here](https://github.com/JackHack96/PulseEffects-Presets) when headphones are plugged in and stops it when said headphones are removed
* sets headphones' volume to 40% if mpv is started, to avoid blasting your ears off (I keep MPV blacklisted in PulseEffects)

# Peeks

A twin-peak resonant filter for VCV Rack 2: two resonant 3-pole low-pass filters with one subtracted from the other, giving a band-pass with a ringing peak at each edge. The peaks can cross, cancel when they meet, and ring like struck objects when pinged. Inspired by the twin-peak filter designed by the late Rob Hordijk; an independent behavioural re-imagining.

- Manual: [docs/MANUAL.md](docs/MANUAL.md) and [docs/Peeks-Manual.pdf](docs/Peeks-Manual.pdf)
- Demo: [docs/peeks-demo.wav](docs/peeks-demo.wav)
- Design and research: [docs/BUILD-INSTRUCTIONS.md](docs/BUILD-INSTRUCTIONS.md), [docs/RESEARCH.md](docs/RESEARCH.md), [docs/DECISIONS.md](docs/DECISIONS.md)

## Install (Apple silicon Mac)

Download `Peeks-2.0.0-mac-arm64.vcvplugin` from the [latest release](https://github.com/hardyfinn/peeks/releases/latest) and drop it into Rack's plugins folder (`~/Library/Application Support/Rack2/plugins-mac-arm64/`), or double-click it with Rack closed. Restart Rack and search for "Peeks".

## Build

```
export RACK_DIR=/path/to/Rack-SDK
make install
make test      # host tests of the DSP engine, no SDK needed
make demo      # renders docs/peeks-demo.wav
make panels    # regenerates res/Peeks.svg and src/generated/Layout.hpp (needs fonttools)
```

GPL-3.0-or-later.

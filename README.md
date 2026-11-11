# Rattlegram

Transceive UTF-8 text messages with up to 170 bytes over audio.

This repository contains the original Android application and a native Linux
command-line interface. Both use the same C++17 encoder and decoder, so their
audio is compatible.

## Linux CLI

The CLI supports WAV files, standard input/output, live PulseAudio capture and
playback, pings, and a repeater with duplicate suppression.

### Dependencies

- A C++17 compiler
- GNU Make
- `pkg-config`
- PulseAudio development files (`libpulse-simple` and `libpulse`)

PulseAudio also works with the compatibility layer provided by PipeWire. WAV
input and output do not require an additional audio-file library.

On Debian or Ubuntu, install the build dependencies with:

```sh
sudo apt install g++ make pkg-config libpulse-dev
```

### Build and install

```sh
make
./rattlegram --help
```

To install under `/usr/local/bin`:

```sh
sudo make install
```

Use `PREFIX="$HOME/.local" make install` for a user-local installation.

### Transmit and receive WAV files

```sh
./rattlegram tx --call TEST --message "Hola Linux" --output message.wav
./rattlegram rx --input message.wav
```

The received message is printed as tab-separated fields:

```text
received	TEST	Hola Linux
```

Use `--json` for JSON lines with the call sign, mode, frequency offset, and
number of corrected bits:

```sh
./rattlegram rx --input message.wav --json
```

The WAV format is PCM signed 16-bit, mono or stereo. Supported sample rates are
8000, 16000, 32000, 44100, and 48000 Hz. The receiver reads the rate from the
WAV header. Stereo files default to the left channel; select another mode with
`--channel right`, `--channel sum`, or `--channel analytic`.

### Pipes

A message supplied with `--message` is used verbatim. If `tx` has no message
argument, it reads standard input and removes one trailing newline. Use `-` as
the input or output filename for WAV data on stdin or stdout.

```sh
printf 'Hola Linux' | ./rattlegram tx --call TEST --output - > message.wav
./rattlegram rx --input - < message.wav
```

Diagnostics go to stderr, so they do not corrupt WAV data or received messages
on stdout.

### Live audio

Without `--output`, `tx` and `ping` play through the default PulseAudio output.
`listen` captures continuously from the default input until Ctrl-C.

```sh
./rattlegram tx --call TEST --message "Hola Linux"
./rattlegram ping --call TEST
./rattlegram listen --rate 8000 --channel mono
```

Select a PulseAudio device with `--device NAME`. Use `--verbose` to report
synchronization and preamble failures.

### Repeater

```sh
./rattlegram repeat --delay 1 --debounce 60
```

The repeater stops capturing during playback and resumes afterward. It preserves
the sender's call sign and payload. Messages with the same call sign and payload
are ignored during the debounce window. Use separate devices or channel options
when needed:

```sh
./rattlegram repeat \
  --input-device INPUT_NAME --output-device OUTPUT_NAME \
  --input-channel mono --output-channel mono
```

### Transmission settings

```sh
./rattlegram tx --call TEST --message "Hola" \
  --rate 48000 --carrier 1500 --noise 6 --channel mono
```

- Messages are limited to **170 UTF-8 bytes**, not 170 Unicode characters.
- Call signs contain 1-9 ASCII letters or digits.
- `--noise 0` disables the initial noise symbols.
- `--fancy-header` enables the visual call-sign header.
- `--ultrasonic` permits carriers above 3000 Hz when the sample rate allows it.
- `--channel analytic` transmits or receives complex stereo I/Q audio.

Run `./rattlegram --help` for the complete command reference.

## Android application

The Android project remains in `app/` and builds with Gradle and the Android
NDK. Java manages the UI and Android audio devices; JNI connects it to the same
native encoder and decoder used by the CLI. The original Makefile workflows are
available as `android`, `android-build`, `android-install`, `android-remove`,
and `android-start`.

## License and attribution

The original Rattlegram code is Copyright (C) 2022 Ahmet Inan. See `LICENSE`
for the permission and disclaimer notices and `NOTICE` for the original project
attribution.

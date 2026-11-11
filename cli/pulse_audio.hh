#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <pulse/simple.h>

namespace rattlegram {

class Playback {
	pa_simple *stream = nullptr;
	size_t frame_size = 0;

public:
	Playback(int rate, int channels, const std::string &device);
	~Playback();
	Playback(const Playback &) = delete;
	Playback &operator=(const Playback &) = delete;
	void write(const int16_t *samples, size_t frames);
	void drain();
};

class Capture {
	pa_simple *stream = nullptr;
	size_t frame_size = 0;

public:
	Capture(int rate, int channels, const std::string &device);
	~Capture();
	Capture(const Capture &) = delete;
	Capture &operator=(const Capture &) = delete;
	void read(int16_t *samples, size_t frames);
};

}

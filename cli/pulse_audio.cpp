#include "pulse_audio.hh"

#include <pulse/error.h>
#include <stdexcept>

namespace rattlegram {

namespace {

pa_sample_spec sample_spec(int rate, int channels)
{
	pa_sample_spec spec;
	spec.format = PA_SAMPLE_S16NE;
	spec.rate = rate;
	spec.channels = channels;
	if (!pa_sample_spec_valid(&spec))
		throw std::invalid_argument("invalid PulseAudio sample specification");
	return spec;
}

std::runtime_error pulse_error(const char *operation, int error)
{
	return std::runtime_error(std::string(operation) + ": " + pa_strerror(error));
}

}

Playback::Playback(int rate, int channels, const std::string &device)
{
	auto spec = sample_spec(rate, channels);
	frame_size = pa_frame_size(&spec);
	int error = 0;
	stream = pa_simple_new(nullptr, "rattlegram", PA_STREAM_PLAYBACK, device.empty() ? nullptr : device.c_str(), "message", &spec, nullptr, nullptr, &error);
	if (!stream)
		throw pulse_error("cannot open PulseAudio playback", error);
}

Playback::~Playback()
{
	if (stream)
		pa_simple_free(stream);
}

void Playback::write(const int16_t *samples, size_t frames)
{
	int error = 0;
	if (pa_simple_write(stream, samples, frames * frame_size, &error) < 0)
		throw pulse_error("PulseAudio playback failed", error);
}

void Playback::drain()
{
	int error = 0;
	if (pa_simple_drain(stream, &error) < 0)
		throw pulse_error("PulseAudio drain failed", error);
}

Capture::Capture(int rate, int channels, const std::string &device)
{
	auto spec = sample_spec(rate, channels);
	frame_size = pa_frame_size(&spec);
	int error = 0;
	stream = pa_simple_new(nullptr, "rattlegram", PA_STREAM_RECORD, device.empty() ? nullptr : device.c_str(), "listen", &spec, nullptr, nullptr, &error);
	if (!stream)
		throw pulse_error("cannot open PulseAudio capture", error);
}

Capture::~Capture()
{
	if (stream)
		pa_simple_free(stream);
}

void Capture::read(int16_t *samples, size_t frames)
{
	int error = 0;
	if (pa_simple_read(stream, samples, frames * frame_size, &error) < 0)
		throw pulse_error("PulseAudio capture failed", error);
}

}

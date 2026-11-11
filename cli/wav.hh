#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rattlegram {

struct WavData {
	int rate = 0;
	int channels = 0;
	std::vector<int16_t> samples;
};

WavData read_wav(const std::string &path);
void write_wav(const std::string &path, int rate, int channels, const std::vector<int16_t> &samples);

}

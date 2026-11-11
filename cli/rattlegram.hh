#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <new>
#include <string>
#include <vector>

struct DecoderInterface;

namespace rattlegram {

constexpr int payload_size = 170;
constexpr int call_size = 10;

struct TransmitOptions {
	int rate = 8000;
	int channel = 0;
	int carrier_frequency = 1500;
	int noise_symbols = 6;
	bool fancy_header = false;
};

struct Message {
	std::array<uint8_t, call_size> call{};
	std::array<uint8_t, payload_size> payload{};
	int mode = 0;
	float carrier_frequency_offset = 0;
	int corrections = 0;

	std::string call_sign() const;
	std::string text() const;
};

struct DecoderCallbacks {
	std::function<void(const Message &)> message;
	std::function<void(const std::string &, float)> ping;
	std::function<void(const std::string &, int, float)> unsupported;
	std::function<void(const std::string &, int, float)> sync;
	std::function<void()> failure;
};

int extended_length(int rate);
std::vector<int16_t> encode(const std::string &message, const std::string &call_sign, const TransmitOptions &options);

class DecoderSession {
	std::unique_ptr<DecoderInterface> decoder;
	int sample_rate;
	float staged_cfo = 0;
	int32_t staged_mode = 0;
	std::array<uint8_t, call_size> staged_call{};

	void update_staged();
	void process(const DecoderCallbacks &callbacks);

public:
	explicit DecoderSession(int rate);
	~DecoderSession();
	void feed(const int16_t *samples, int count, int channel, const DecoderCallbacks &callbacks);
};

}

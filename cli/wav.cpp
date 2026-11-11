#include "wav.hh"

#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <vector>

namespace rattlegram {

namespace {

uint16_t u16(const std::vector<uint8_t> &data, size_t offset)
{
	return uint16_t(data[offset]) | uint16_t(data[offset + 1]) << 8;
}

uint32_t u32(const std::vector<uint8_t> &data, size_t offset)
{
	return uint32_t(u16(data, offset)) | uint32_t(u16(data, offset + 2)) << 16;
}

void append_u16(std::vector<uint8_t> &data, uint16_t value)
{
	data.push_back(value & 255);
	data.push_back(value >> 8);
}

void append_u32(std::vector<uint8_t> &data, uint32_t value)
{
	append_u16(data, value & 65535);
	append_u16(data, value >> 16);
}

void append_tag(std::vector<uint8_t> &data, const char *tag)
{
	data.insert(data.end(), tag, tag + 4);
}

std::vector<uint8_t> read_bytes(std::istream &input)
{
	return std::vector<uint8_t>(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

WavData parse_wav(const std::vector<uint8_t> &bytes)
{
	if (bytes.size() < 44 || std::string(bytes.begin(), bytes.begin() + 4) != "RIFF" || std::string(bytes.begin() + 8, bytes.begin() + 12) != "WAVE")
		throw std::runtime_error("input is not a RIFF/WAVE file");
	bool have_format = false;
	bool have_data = false;
	uint16_t format = 0;
	uint16_t channels = 0;
	uint16_t bits = 0;
	uint16_t block_align = 0;
	uint32_t rate = 0;
	size_t data_offset = 0;
	size_t data_size = 0;
	for (size_t offset = 12; offset + 8 <= bytes.size();) {
		uint32_t chunk_size = u32(bytes, offset + 4);
		size_t payload = offset + 8;
		if (chunk_size > bytes.size() - payload)
			throw std::runtime_error("truncated WAV chunk");
		std::string tag(bytes.begin() + offset, bytes.begin() + offset + 4);
		if (tag == "fmt ") {
			if (chunk_size < 16)
				throw std::runtime_error("invalid WAV format chunk");
			format = u16(bytes, payload);
			channels = u16(bytes, payload + 2);
			rate = u32(bytes, payload + 4);
			block_align = u16(bytes, payload + 12);
			bits = u16(bytes, payload + 14);
			if (format == 0xfffe && chunk_size >= 40)
				format = u16(bytes, payload + 24);
			have_format = true;
		} else if (tag == "data" && !have_data) {
			data_offset = payload;
			data_size = chunk_size;
			have_data = true;
		}
		offset = payload + chunk_size + (chunk_size & 1);
	}
	if (!have_format || !have_data)
		throw std::runtime_error("WAV file lacks a format or data chunk");
	if (format != 1 || bits != 16)
		throw std::runtime_error("only 16-bit PCM WAV input is supported");
	if (channels != 1 && channels != 2)
		throw std::runtime_error("only mono and stereo WAV input is supported");
	if (block_align != channels * 2 || data_size % block_align != 0)
		throw std::runtime_error("invalid WAV sample alignment");
	WavData wav;
	wav.rate = static_cast<int>(rate);
	wav.channels = channels;
	wav.samples.resize(data_size / 2);
	for (size_t i = 0; i < wav.samples.size(); ++i)
		wav.samples[i] = static_cast<int16_t>(u16(bytes, data_offset + 2 * i));
	return wav;
}

}

WavData read_wav(const std::string &path)
{
	if (path == "-") {
		auto bytes = read_bytes(std::cin);
		if (!std::cin.eof() && std::cin.fail())
			throw std::runtime_error("failed to read WAV from standard input");
		return parse_wav(bytes);
	}
	std::ifstream input(path, std::ios::binary);
	if (!input)
		throw std::runtime_error("cannot open input file: " + path);
	auto bytes = read_bytes(input);
	if (input.bad())
		throw std::runtime_error("failed to read input file: " + path);
	return parse_wav(bytes);
}

void write_wav(const std::string &path, int rate, int channels, const std::vector<int16_t> &samples)
{
	if (channels != 1 && channels != 2)
		throw std::invalid_argument("WAV output must be mono or stereo");
	if (samples.size() % channels != 0)
		throw std::invalid_argument("misaligned PCM output");
	if (samples.size() > (std::numeric_limits<uint32_t>::max() - 36) / 2)
		throw std::length_error("WAV output exceeds the RIFF size limit");
	std::vector<uint8_t> bytes;
	bytes.reserve(44 + samples.size() * 2);
	append_tag(bytes, "RIFF");
	append_u32(bytes, 36 + samples.size() * 2);
	append_tag(bytes, "WAVE");
	append_tag(bytes, "fmt ");
	append_u32(bytes, 16);
	append_u16(bytes, 1);
	append_u16(bytes, channels);
	append_u32(bytes, rate);
	append_u32(bytes, rate * channels * 2);
	append_u16(bytes, channels * 2);
	append_u16(bytes, 16);
	append_tag(bytes, "data");
	append_u32(bytes, samples.size() * 2);
	for (int16_t sample : samples)
		append_u16(bytes, static_cast<uint16_t>(sample));
	if (path == "-") {
		std::cout.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
		std::cout.flush();
		if (!std::cout)
			throw std::runtime_error("failed to write WAV to standard output");
		return;
	}
	std::ofstream output(path, std::ios::binary);
	if (!output)
		throw std::runtime_error("cannot open output file: " + path);
	output.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
	if (!output)
		throw std::runtime_error("failed to write output file: " + path);
}

}

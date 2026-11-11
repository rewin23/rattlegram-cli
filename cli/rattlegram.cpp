#include "rattlegram.hh"

#include <stdexcept>

#include "encoder.hh"
#include "decoder.hh"

namespace rattlegram {

namespace {

std::unique_ptr<EncoderInterface> make_encoder(int rate)
{
	switch (rate) {
		case 8000: return std::make_unique<Encoder<8000>>();
		case 16000: return std::make_unique<Encoder<16000>>();
		case 32000: return std::make_unique<Encoder<32000>>();
		case 44100: return std::make_unique<Encoder<44100>>();
		case 48000: return std::make_unique<Encoder<48000>>();
		default: throw std::invalid_argument("unsupported sample rate");
	}
}

std::unique_ptr<DecoderInterface> make_decoder(int rate)
{
	switch (rate) {
		case 8000: return std::make_unique<Decoder<8000>>();
		case 16000: return std::make_unique<Decoder<16000>>();
		case 32000: return std::make_unique<Decoder<32000>>();
		case 44100: return std::make_unique<Decoder<44100>>();
		case 48000: return std::make_unique<Decoder<48000>>();
		default: throw std::invalid_argument("unsupported sample rate");
	}
}

}

std::string Message::call_sign() const
{
	size_t length = 0;
	while (length < call.size() && call[length])
		++length;
	std::string value(reinterpret_cast<const char *>(call.data()), length);
	auto first = value.find_first_not_of(' ');
	if (first == std::string::npos)
		return {};
	auto last = value.find_last_not_of(' ');
	return value.substr(first, last - first + 1);
}

std::string Message::text() const
{
	size_t length = 0;
	while (length < payload.size() && payload[length])
		++length;
	return std::string(reinterpret_cast<const char *>(payload.data()), length);
}

int extended_length(int rate)
{
	int symbol_length = (1280 * rate) / 8000;
	return symbol_length + symbol_length / 8;
}

std::vector<int16_t> encode(const std::string &message, const std::string &call_sign, const TransmitOptions &options)
{
	if (message.size() > payload_size)
		throw std::invalid_argument("message exceeds 170 UTF-8 bytes");
	if (call_sign.empty() || call_sign.size() >= call_size)
		throw std::invalid_argument("call sign must contain between 1 and 9 characters");
	auto encoder = make_encoder(options.rate);
	std::array<uint8_t, payload_size> payload{};
	std::array<int8_t, call_size> call{};
	std::memcpy(payload.data(), message.data(), message.size());
	std::memcpy(call.data(), call_sign.data(), call_sign.size());
	encoder->configure(payload.data(), call.data(), options.carrier_frequency, options.noise_symbols, options.fancy_header);
	std::vector<int16_t> block(extended_length(options.rate) * (options.channel == 0 ? 1 : 2));
	std::vector<int16_t> audio;
	while (encoder->produce(block.data(), options.channel))
		audio.insert(audio.end(), block.begin(), block.end());
	return audio;
}

DecoderSession::DecoderSession(int rate) : decoder(make_decoder(rate)), sample_rate(rate)
{
}

DecoderSession::~DecoderSession() = default;

void DecoderSession::update_staged()
{
	decoder->staged(&staged_cfo, &staged_mode, staged_call.data());
}

void DecoderSession::process(const DecoderCallbacks &callbacks)
{
	int status = decoder->process();
	switch (status) {
		case STATUS_OKAY:
			break;
		case STATUS_FAIL:
			if (callbacks.failure)
				callbacks.failure();
			break;
		case STATUS_SYNC:
			update_staged();
			if (callbacks.sync)
				callbacks.sync(Message{staged_call}.call_sign(), staged_mode, staged_cfo);
			break;
		case STATUS_DONE: {
			Message message;
			message.call = staged_call;
			message.mode = staged_mode;
			message.carrier_frequency_offset = staged_cfo;
			message.corrections = decoder->fetch(message.payload.data());
			if (message.corrections < 0)
				throw std::runtime_error("payload decoding failed");
			if (callbacks.message)
				callbacks.message(message);
			break;
		}
		case STATUS_HEAP:
			throw std::runtime_error("decoder allocation failed");
		case STATUS_NOPE:
		case STATUS_PING: {
			update_staged();
			std::string call = Message{staged_call}.call_sign();
			if (status == STATUS_PING) {
				if (callbacks.ping)
					callbacks.ping(call, staged_cfo);
			} else if (callbacks.unsupported) {
				callbacks.unsupported(call, staged_mode, staged_cfo);
			}
			break;
		}
		default:
			throw std::runtime_error("unknown decoder status");
	}
}

void DecoderSession::feed(const int16_t *samples, int count, int channel, const DecoderCallbacks &callbacks)
{
	if (count <= 0)
		return;
	if (count > extended_length(sample_rate))
		throw std::invalid_argument("audio block is too large");
	if (decoder->feed(samples, count, channel))
		process(callbacks);
}

}

#include "pulse_audio.hh"
#include "rattlegram.hh"
#include "wav.hh"

#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

volatile std::sig_atomic_t interrupted = 0;

void stop_handler(int)
{
	interrupted = 1;
}

struct Options {
	std::string command;
	std::string message;
	std::string input = "-";
	std::string output;
	std::string device;
	std::string input_device;
	std::string output_device;
	std::string call = "ANONYMOUS";
	int rate = 8000;
	int input_channel = 0;
	int output_channel = 0;
	int carrier = 1500;
	int noise = 6;
	int delay = 1;
	int debounce = 60;
	bool message_given = false;
	bool positional_message = false;
	bool fancy = false;
	bool ultrasonic = false;
	bool json = false;
	bool verbose = false;
	bool show_help = false;
	bool show_version = false;
};

struct EventCount {
	int messages = 0;
	int pings = 0;
};

void print_help()
{
	std::cout <<
		"Rattlegram CLI - transceive UTF-8 text messages over audio\n\n"
		"Usage:\n"
		"  rattlegram tx [options] [message]\n"
		"  rattlegram rx [--input FILE|-] [--channel MODE] [--json] [--verbose]\n"
		"  rattlegram listen [options]\n"
		"  rattlegram ping [options]\n"
		"  rattlegram repeat [options]\n\n"
		"Transmit options (tx, ping):\n"
		"  -m, --message TEXT       Message text; tx reads stdin when omitted\n"
		"  -o, --output FILE|-      Write WAV instead of using the speaker\n"
		"      --call SIGN          Call sign, 1-9 alphanumeric characters\n"
		"      --device NAME        PulseAudio playback device\n"
		"  -r, --rate HZ            8000, 16000, 32000, 44100 or 48000\n"
		"  -c, --channel MODE       mono, left, right or analytic\n"
		"      --carrier HZ         Carrier frequency, default 1500\n"
		"      --noise COUNT        Noise symbols before the message, default 6\n"
		"      --fancy-header       Encode the call sign as a visual header\n"
		"      --ultrasonic         Allow carrier frequencies above 3000 Hz\n\n"
		"Receive options:\n"
		"  -i, --input FILE|-       Input WAV for rx; default is stdin\n"
		"      --device NAME        PulseAudio capture device for listen\n"
		"  -r, --rate HZ            Capture rate for listen and repeat\n"
		"  -c, --channel MODE       mono, left, right, sum or analytic\n"
		"      --json               Emit received events as JSON lines\n"
		"      --verbose            Report synchronization and failures\n\n"
		"Repeater options:\n"
		"      --input-device NAME  PulseAudio capture device\n"
		"      --output-device NAME PulseAudio playback device\n"
		"      --input-channel MODE Capture channel mode\n"
		"      --output-channel MODE Playback channel mode\n"
		"      --delay SECONDS      Delay before repeating, default 1\n"
		"      --debounce SECONDS   Duplicate suppression window, default 60\n\n"
		"Channel aliases: default=mono, first=left, second=right.\n"
		"Use --channel analytic for complex stereo (I/Q) audio.\n";
}

int parse_integer(const std::string &value, const std::string &option)
{
	int result = 0;
	auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
	if (parsed.ec != std::errc() || parsed.ptr != value.data() + value.size())
		throw std::invalid_argument("invalid integer for " + option + ": " + value);
	return result;
}

int parse_channel(const std::string &value)
{
	if (value == "default" || value == "mono") return 0;
	if (value == "first" || value == "left") return 1;
	if (value == "second" || value == "right") return 2;
	if (value == "sum") return 3;
	if (value == "analytic") return 4;
	throw std::invalid_argument("unknown channel mode: " + value);
}

std::string option_value(int argc, char **argv, int &index, const std::string &option)
{
	if (index + 1 >= argc)
		throw std::invalid_argument("missing value for " + option);
	return argv[++index];
}

Options parse_options(int argc, char **argv)
{
	Options options;
	if (argc < 2) {
		options.show_help = true;
		return options;
	}
	options.command = argv[1];
	if (options.command == "--help" || options.command == "-h" || options.command == "help") {
		options.show_help = true;
		return options;
	}
	if (options.command == "--version" || options.command == "version") {
		options.show_version = true;
		return options;
	}
	if (options.command != "tx" && options.command != "rx" && options.command != "listen" && options.command != "ping" && options.command != "repeat")
		throw std::invalid_argument("unknown command: " + options.command);
	bool tx = options.command == "tx";
	bool ping = options.command == "ping";
	bool rx = options.command == "rx";
	bool listen = options.command == "listen";
	bool repeat = options.command == "repeat";
	bool transmit = tx || ping || repeat;
	bool capture = rx || listen || repeat;
	if (rx)
		options.input_channel = -1;
	for (int i = 2; i < argc; ++i) {
		std::string argument = argv[i];
		if (argument == "--help" || argument == "-h") {
			options.show_help = true;
			return options;
		}
		if (argument == "--version") {
			options.show_version = true;
			return options;
		}
		if ((argument == "--message" || argument == "-m") && tx) {
			options.message = option_value(argc, argv, i, argument);
			options.message_given = true;
		} else if ((argument == "--output" || argument == "-o") && (tx || ping)) {
			options.output = option_value(argc, argv, i, argument);
		} else if ((argument == "--input" || argument == "-i") && rx) {
			options.input = option_value(argc, argv, i, argument);
		} else if (argument == "--call" && (tx || ping)) {
			options.call = option_value(argc, argv, i, argument);
		} else if (argument == "--device" && (tx || ping || listen)) {
			options.device = option_value(argc, argv, i, argument);
		} else if (argument == "--input-device" && repeat) {
			options.input_device = option_value(argc, argv, i, argument);
		} else if (argument == "--output-device" && repeat) {
			options.output_device = option_value(argc, argv, i, argument);
		} else if ((argument == "--rate" || argument == "-r") && !rx) {
			options.rate = parse_integer(option_value(argc, argv, i, argument), argument);
		} else if ((argument == "--channel" || argument == "-c") && (tx || ping)) {
			options.output_channel = parse_channel(option_value(argc, argv, i, argument));
		} else if ((argument == "--channel" || argument == "-c") && capture) {
			options.input_channel = parse_channel(option_value(argc, argv, i, argument));
		} else if (argument == "--input-channel" && repeat) {
			options.input_channel = parse_channel(option_value(argc, argv, i, argument));
		} else if (argument == "--output-channel" && repeat) {
			options.output_channel = parse_channel(option_value(argc, argv, i, argument));
		} else if (argument == "--carrier" && transmit) {
			options.carrier = parse_integer(option_value(argc, argv, i, argument), argument);
		} else if (argument == "--noise" && transmit) {
			options.noise = parse_integer(option_value(argc, argv, i, argument), argument);
		} else if (argument == "--delay" && repeat) {
			options.delay = parse_integer(option_value(argc, argv, i, argument), argument);
		} else if (argument == "--debounce" && repeat) {
			options.debounce = parse_integer(option_value(argc, argv, i, argument), argument);
		} else if (argument == "--fancy-header" && transmit) {
			options.fancy = true;
		} else if (argument == "--ultrasonic" && transmit) {
			options.ultrasonic = true;
		} else if (argument == "--json" && capture) {
			options.json = true;
		} else if (argument == "--verbose" && capture) {
			options.verbose = true;
		} else if (tx && !argument.empty() && argument[0] != '-' && !options.positional_message) {
			options.message = argument;
			options.message_given = true;
			options.positional_message = true;
		} else {
			throw std::invalid_argument("unknown or misplaced option: " + argument);
		}
	}
	return options;
}

void validate_rate(int rate)
{
	switch (rate) {
		case 8000:
		case 16000:
		case 32000:
		case 44100:
		case 48000:
			return;
		default:
			throw std::invalid_argument("sample rate must be 8000, 16000, 32000, 44100 or 48000");
	}
}

void validate_transmit(const Options &options)
{
	validate_rate(options.rate);
	if (options.output_channel == 3)
		throw std::invalid_argument("sum is only valid for captured audio");
	if (options.noise < 0)
		throw std::invalid_argument("noise symbol count cannot be negative");
	if (options.call.empty() || options.call.size() > 9)
		throw std::invalid_argument("call sign must contain between 1 and 9 characters");
	for (char character : options.call) {
		unsigned char value = static_cast<unsigned char>(character);
		if (!((value >= '0' && value <= '9') || (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z')))
			throw std::invalid_argument("call sign accepts only ASCII letters and digits");
	}
	int maximum = (options.rate - 1600) / 2;
	if (!options.ultrasonic)
		maximum = std::min(maximum, 3000);
	int minimum = options.output_channel == 4 ? -maximum : 1000;
	if (options.carrier < minimum || options.carrier > maximum)
		throw std::invalid_argument("carrier is outside the allowed range for this rate and channel");
}

std::string read_stdin_message()
{
	std::string message(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
	if (!message.empty() && message.back() == '\n') {
		message.pop_back();
		if (!message.empty() && message.back() == '\r')
			message.pop_back();
	}
	return message;
}

std::string json_escape(const std::string &value)
{
	static const char hex[] = "0123456789abcdef";
	std::string escaped;
	for (unsigned char character : value) {
		switch (character) {
			case '"': escaped += "\\\""; break;
			case '\\': escaped += "\\\\"; break;
			case '\b': escaped += "\\b"; break;
			case '\f': escaped += "\\f"; break;
			case '\n': escaped += "\\n"; break;
			case '\r': escaped += "\\r"; break;
			case '\t': escaped += "\\t"; break;
			default:
				if (character < 32) {
					escaped += "\\u00";
					escaped += hex[character >> 4];
					escaped += hex[character & 15];
				} else {
					escaped += static_cast<char>(character);
				}
		}
	}
	return escaped;
}

void print_message(const rattlegram::Message &message, const Options &options, const std::string &action = "received")
{
	if (options.json) {
		std::cout << "{\"event\":\"" << action << "\",\"call\":\"" << json_escape(message.call_sign())
			<< "\",\"mode\":" << message.mode << ",\"cfo_hz\":" << message.carrier_frequency_offset
			<< ",\"corrections\":" << message.corrections << ",\"message\":\"" << json_escape(message.text()) << "\"}" << std::endl;
	} else {
		std::cout << action << '\t' << message.call_sign() << '\t' << message.text() << std::endl;
	}
}

rattlegram::DecoderCallbacks make_callbacks(const Options &options, EventCount &events)
{
	rattlegram::DecoderCallbacks callbacks;
	callbacks.message = [&](const rattlegram::Message &message) {
		++events.messages;
		print_message(message, options);
	};
	callbacks.ping = [&](const std::string &call, float cfo) {
		++events.pings;
		if (options.json)
			std::cout << "{\"event\":\"ping\",\"call\":\"" << json_escape(call) << "\",\"cfo_hz\":" << cfo << "}" << std::endl;
		else
			std::cout << "ping\t" << call << std::endl;
	};
	callbacks.unsupported = [&](const std::string &call, int mode, float cfo) {
		if (options.json)
			std::cout << "{\"event\":\"unsupported\",\"call\":\"" << json_escape(call) << "\",\"mode\":" << mode << ",\"cfo_hz\":" << cfo << "}" << std::endl;
		else
			std::cout << "unsupported\t" << call << '\t' << mode << std::endl;
	};
	if (options.verbose) {
		callbacks.sync = [](const std::string &call, int mode, float cfo) {
			std::cerr << "sync: call=" << call << " mode=" << mode << " cfo=" << cfo << " Hz\n";
		};
		callbacks.failure = []() {
			std::cerr << "preamble decoding failed\n";
		};
	}
	return callbacks;
}

void feed_audio(rattlegram::DecoderSession &session, int rate, int channels, const std::vector<int16_t> &samples, int channel, const rattlegram::DecoderCallbacks &callbacks)
{
	int block_frames = rate / 50;
	for (size_t offset = 0; offset < samples.size(); offset += block_frames * channels) {
		size_t remaining = samples.size() - offset;
		int frames = static_cast<int>(std::min<size_t>(block_frames, remaining / channels));
		if (frames > 0)
			session.feed(samples.data() + offset, frames, channel, callbacks);
	}
}

void play_audio(const std::vector<int16_t> &samples, const Options &options, const std::string &device)
{
	int channels = options.output_channel == 0 ? 1 : 2;
	rattlegram::Playback playback(options.rate, channels, device);
	playback.write(samples.data(), samples.size() / channels);
	playback.drain();
}

rattlegram::TransmitOptions transmit_options(const Options &options)
{
	rattlegram::TransmitOptions transmit;
	transmit.rate = options.rate;
	transmit.channel = options.output_channel;
	transmit.carrier_frequency = options.carrier;
	transmit.noise_symbols = options.noise;
	transmit.fancy_header = options.fancy;
	return transmit;
}

int run_tx(Options options, bool ping)
{
	validate_transmit(options);
	if (!ping) {
		if (!options.message_given)
			options.message = read_stdin_message();
		if (options.message.empty())
			throw std::invalid_argument("tx requires a nonempty message; use ping for an empty payload");
		if (options.message.size() > rattlegram::payload_size)
			throw std::invalid_argument("message exceeds 170 UTF-8 bytes");
	}
	if (options.message.find('\0') != std::string::npos)
		throw std::invalid_argument("message cannot contain a null byte");
	auto samples = rattlegram::encode(options.message, options.call, transmit_options(options));
	int channels = options.output_channel == 0 ? 1 : 2;
	if (!options.output.empty()) {
		rattlegram::write_wav(options.output, options.rate, channels, samples);
	} else {
		play_audio(samples, options, options.device);
	}
	std::cerr << (ping ? "ping" : "message") << " encoded: " << samples.size() / channels << " frames at " << options.rate << " Hz\n";
	return 0;
}

int run_rx(const Options &options)
{
	auto wav = rattlegram::read_wav(options.input);
	validate_rate(wav.rate);
	int channel = options.input_channel;
	if (channel < 0)
		channel = wav.channels == 1 ? 0 : 1;
	if ((channel == 0 && wav.channels != 1) || (channel != 0 && wav.channels != 2))
		throw std::invalid_argument("selected channel mode does not match the WAV channel count");
	EventCount events;
	auto callbacks = make_callbacks(options, events);
	rattlegram::DecoderSession session(wav.rate);
	size_t padding = 4 * rattlegram::extended_length(wav.rate) * wav.channels;
	std::vector<int16_t> samples(padding, 0);
	samples.insert(samples.end(), wav.samples.begin(), wav.samples.end());
	samples.insert(samples.end(), padding, 0);
	feed_audio(session, wav.rate, wav.channels, samples, channel, callbacks);
	return events.messages + events.pings == 0 ? 1 : 0;
}

void install_signal_handlers()
{
	std::signal(SIGINT, stop_handler);
	std::signal(SIGTERM, stop_handler);
}

int run_listen(const Options &options)
{
	validate_rate(options.rate);
	int channels = options.input_channel == 0 ? 1 : 2;
	EventCount events;
	auto callbacks = make_callbacks(options, events);
	rattlegram::DecoderSession session(options.rate);
	rattlegram::Capture capture(options.rate, channels, options.device);
	int block_frames = options.rate / 50;
	std::vector<int16_t> block(block_frames * channels);
	install_signal_handlers();
	std::cerr << "listening at " << options.rate << " Hz; press Ctrl-C to stop\n";
	try {
		while (!interrupted) {
			capture.read(block.data(), block_frames);
			session.feed(block.data(), block_frames, options.input_channel, callbacks);
		}
	} catch (...) {
		if (!interrupted)
			throw;
	}
	return 0;
}

int run_repeat(const Options &options)
{
	validate_transmit(options);
	if (options.delay < 0 || options.debounce < 0)
		throw std::invalid_argument("repeater delay and debounce cannot be negative");
	int input_channels = options.input_channel == 0 ? 1 : 2;
	EventCount events;
	auto callbacks = make_callbacks(options, events);
	std::optional<rattlegram::Message> pending;
	struct Repeated {
		std::chrono::steady_clock::time_point time;
		rattlegram::Message message;
	};
	std::vector<Repeated> repeated;
	callbacks.message = [&](const rattlegram::Message &message) {
		auto now = std::chrono::steady_clock::now();
		for (auto item = repeated.begin(); item != repeated.end();) {
			auto age = std::chrono::duration_cast<std::chrono::seconds>(now - item->time).count();
			if (age >= options.debounce) {
				item = repeated.erase(item);
				continue;
			}
			if (item->message.call == message.call && item->message.payload == message.payload) {
				if (options.verbose)
					std::cerr << "ignoring duplicate message from " << message.call_sign() << '\n';
				return;
			}
			++item;
		}
		if (options.debounce > 0)
			repeated.push_back({now, message});
		++events.messages;
		pending = message;
	};
	auto session = std::make_unique<rattlegram::DecoderSession>(options.rate);
	auto capture = std::make_unique<rattlegram::Capture>(options.rate, input_channels, options.input_device);
	int block_frames = options.rate / 50;
	std::vector<int16_t> block(block_frames * input_channels);
	install_signal_handlers();
	std::cerr << "repeater listening at " << options.rate << " Hz; press Ctrl-C to stop\n";
	try {
		while (!interrupted) {
			capture->read(block.data(), block_frames);
			session->feed(block.data(), block_frames, options.input_channel, callbacks);
			if (!pending)
				continue;
			auto message = *pending;
			pending.reset();
			capture.reset();
			if (options.delay > 0)
				std::this_thread::sleep_for(std::chrono::seconds(options.delay));
			if (interrupted)
				break;
			auto samples = rattlegram::encode(message.text(), message.call_sign(), transmit_options(options));
			play_audio(samples, options, options.output_device);
			print_message(message, options, "repeated");
			std::this_thread::sleep_for(std::chrono::seconds(1));
			if (interrupted)
				break;
			session = std::make_unique<rattlegram::DecoderSession>(options.rate);
			capture = std::make_unique<rattlegram::Capture>(options.rate, input_channels, options.input_device);
			std::cerr << "repeater listening\n";
		}
	} catch (...) {
		if (!interrupted)
			throw;
	}
	return 0;
}

}

int main(int argc, char **argv)
{
	try {
		auto options = parse_options(argc, argv);
		if (options.show_help) {
			print_help();
			return 0;
		}
		if (options.show_version) {
			std::cout << "rattlegram-cli 1.0.0 (Rattlegram 1.19 protocol)\n";
			return 0;
		}
		if (options.command == "tx")
			return run_tx(options, false);
		if (options.command == "ping")
			return run_tx(options, true);
		if (options.command == "rx")
			return run_rx(options);
		if (options.command == "listen")
			return run_listen(options);
		if (options.command == "repeat")
			return run_repeat(options);
		return 2;
	} catch (const std::exception &error) {
		std::cerr << "rattlegram: " << error.what() << '\n';
		return 2;
	}
}

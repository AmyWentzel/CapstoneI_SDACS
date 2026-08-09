#include <algorithm>
#include <cmath>
#include <iostream>
#include <iterator>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "edge-impulse-sdk/classifier/ei_run_classifier.h"

namespace {
constexpr double kThreshold = 0.60;
const std::vector<std::string> kFeatureNames = {
    "dbfs_mean", "dbfs_std", "dbfs_range",
    "f_peak_acoustic_hz_mean", "f_peak_acoustic_hz_std", "f_peak_acoustic_hz_range",
    "low_rumble_ratio_mean", "low_rumble_ratio_std", "low_rumble_ratio_range",
    "band_bass_ratio_mean", "band_bass_ratio_std", "band_bass_ratio_range",
    "band_low_mid_ratio_mean", "band_low_mid_ratio_std", "band_low_mid_ratio_range",
    "band_mid_ratio_mean", "band_mid_ratio_std", "band_mid_ratio_range",
    "band_presence_ratio_mean", "band_presence_ratio_std", "band_presence_ratio_range",
    "band_high_ratio_mean", "band_high_ratio_std", "band_high_ratio_range",
    "band_bass_peak_hz_mean", "band_bass_peak_hz_std", "band_bass_peak_hz_range",
    "band_low_mid_peak_hz_mean", "band_low_mid_peak_hz_std", "band_low_mid_peak_hz_range",
    "band_mid_peak_hz_mean", "band_mid_peak_hz_std", "band_mid_peak_hz_range",
    "band_presence_peak_hz_mean", "band_presence_peak_hz_std", "band_presence_peak_hz_range",
    "band_high_peak_hz_mean", "band_high_peak_hz_std", "band_high_peak_hz_range",
    "dominant_band_ratio_mean", "dominant_band_ratio_std", "dominant_band_ratio_range",
    "dominant_band_peak_hz_mean", "dominant_band_peak_hz_std", "dominant_band_peak_hz_range",
    "fft_low_ratio_mean", "fft_low_ratio_std", "fft_low_ratio_range",
    "fft_mid_ratio_mean", "fft_mid_ratio_std", "fft_mid_ratio_range",
    "fft_high_ratio_mean", "fft_high_ratio_std", "fft_high_ratio_range",
    "fft_total_energy_mean", "fft_total_energy_std", "fft_total_energy_range",
};
std::vector<float> g_features;

std::string array_text(const std::string& json, const std::string& key) {
    const std::regex marker("\"" + key + "\"\\s*:\\s*\\[");
    std::smatch match;
    if (!std::regex_search(json, match, marker)) throw std::runtime_error("missing " + key);
    const auto start = static_cast<std::size_t>(match.position() + match.length());
    const auto end = json.find(']', start);
    if (end == std::string::npos) throw std::runtime_error("unterminated " + key);
    return json.substr(start, end - start);
}

std::vector<std::string> parse_names(const std::string& text) {
    std::vector<std::string> values;
    const std::regex item("\"([^\"]*)\"");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), item);
         it != std::sregex_iterator(); ++it) {
        values.push_back((*it)[1].str());
    }
    std::string residue = std::regex_replace(text, item, "");
    residue.erase(std::remove_if(residue.begin(), residue.end(), [](unsigned char c) {
        return std::isspace(c) || c == ',';
    }), residue.end());
    if (!residue.empty()) throw std::runtime_error("feature_names must contain strings only");
    return values;
}

std::vector<float> parse_features(const std::string& text) {
    std::vector<float> values;
    std::stringstream stream(text);
    std::string token;
    while (std::getline(stream, token, ',')) {
        token.erase(0, token.find_first_not_of(" \t\r\n"));
        token.erase(token.find_last_not_of(" \t\r\n") + 1);
        if (token.empty() || token == "null") throw std::runtime_error("features contain null");
        std::size_t consumed = 0;
        float value;
        try {
            value = std::stof(token, &consumed);
        } catch (...) {
            throw std::runtime_error("features must contain numbers only");
        }
        if (consumed != token.size() || !std::isfinite(value)) {
            throw std::runtime_error("features must be finite numbers");
        }
        values.push_back(value);
    }
    return values;
}

int raw_feature_data_get_data(size_t offset, size_t length, float* out_ptr) {
    if (offset + length > g_features.size()) return EIDSP_PARAMETER_INVALID;
    std::copy_n(g_features.begin() + offset, length, out_ptr);
    return EIDSP_OK;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--health") {
        if (EI_CLASSIFIER_PROJECT_ID != 1071949 ||
            EI_CLASSIFIER_PROJECT_DEPLOY_VERSION != 1 ||
            EI_CLASSIFIER_NN_INPUT_FRAME_SIZE != 57 ||
            EI_CLASSIFIER_LABEL_COUNT != 3) {
            std::cerr << "sdacs_ei_runner: unexpected compiled model metadata\n";
            return 3;
        }
        run_classifier_init();
        std::cout
            << "{\"status\":\"ready\",\"project_name\":\"SDACS_V3\","
            << "\"project_id\":1071949,\"impulse_id\":1,\"deploy_version\":1,"
            << "\"feature_count\":57,\"labels\":[\"noisy\","
            << "\"quiet_room_white_noise\",\"speech\"]}\n";
        return 0;
    }
    try {
        const std::string input((std::istreambuf_iterator<char>(std::cin)),
                                std::istreambuf_iterator<char>());
        if (input.empty() || input.front() != '{' || input.back() != '}') {
            throw std::runtime_error("invalid JSON object");
        }
        const auto names = parse_names(array_text(input, "feature_names"));
        g_features = parse_features(array_text(input, "features"));
        if (names != kFeatureNames) throw std::runtime_error("feature_names order mismatch");
        if (g_features.size() != kFeatureNames.size()) throw std::runtime_error("expected 57 features");

        signal_t signal{g_features.size(), raw_feature_data_get_data};
        ei_impulse_result_t result{};
        const EI_IMPULSE_ERROR error = run_classifier(&signal, &result, false);
        if (error != EI_IMPULSE_OK) throw std::runtime_error("run_classifier failed");

        std::vector<std::pair<std::string, float>> probabilities;
        for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; ++i) {
            probabilities.emplace_back(result.classification[i].label,
                                       result.classification[i].value);
        }
        const auto top = std::max_element(
            probabilities.begin(), probabilities.end(),
            [](const auto& left, const auto& right) { return left.second < right.second; });
        std::cout << "{\"status\":\"complete\",\"model\":{\"project_name\":\"SDACS_V3\","
                  << "\"project_id\":1071949,\"impulse_id\":1,\"deploy_version\":1,"
                  << "\"feature_count\":57,\"threshold\":0.6},\"probabilities\":{";
        for (size_t i = 0; i < probabilities.size(); ++i) {
            if (i) std::cout << ',';
            std::cout << '"' << probabilities[i].first << "\":" << probabilities[i].second;
        }
        std::cout << "},\"top_label\":\"" << top->first << "\",\"confidence\":"
                  << top->second << ",\"accepted\":"
                  << (top->second >= kThreshold ? "true" : "false")
                  << ",\"timing_ms\":{\"dsp\":" << result.timing.dsp
                  << ",\"classification\":" << result.timing.classification
                  << ",\"anomaly\":0}}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "sdacs_ei_runner: " << error.what() << '\n';
        return 2;
    }
}

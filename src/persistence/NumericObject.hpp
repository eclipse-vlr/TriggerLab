#pragma once

#include <charconv>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace triggerlab::persistence {

// Deliberately restricted to the flat numeric objects used by our schemas.
// Strings, nested values, escapes, duplicate keys and trailing input are invalid.
class NumericObjectReader {
public:
    explicit NumericObjectReader(std::string_view text) : text_(text) {}

    bool take(char expected) {
        whitespace();
        if (position_ == text_.size() || text_[position_] != expected) return false;
        ++position_;
        return true;
    }

    bool done() {
        whitespace();
        return position_ == text_.size();
    }

    std::optional<std::string> key() {
        if (!take('"')) return {};
        const auto start = position_;
        while (position_ < text_.size() &&
               ((text_[position_] >= 'a' && text_[position_] <= 'z') || text_[position_] == '_')) {
            ++position_;
        }
        if (position_ == start || position_ - start > 64 ||
            position_ == text_.size() || text_[position_++] != '"') return {};
        return std::string(text_.substr(start, position_ - start - 1));
    }

    std::optional<std::string_view> number() {
        whitespace();
        const auto start = position_;
        if (position_ < text_.size() && text_[position_] == '-') ++position_;
        if (!digit()) return {};
        if (text_[position_] == '0') ++position_;
        else digits();
        if (position_ < text_.size() && text_[position_] == '.') {
            ++position_;
            if (!digit()) return {};
            digits();
        }
        if (position_ < text_.size() && (text_[position_] == 'e' || text_[position_] == 'E')) {
            ++position_;
            if (position_ < text_.size() && (text_[position_] == '+' || text_[position_] == '-')) ++position_;
            if (!digit()) return {};
            digits();
        }
        return text_.substr(start, position_ - start);
    }

private:
    bool digit() const {
        return position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9';
    }
    void digits() { while (digit()) ++position_; }
    void whitespace() {
        while (position_ < text_.size()) {
            const auto c = text_[position_];
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
            ++position_;
        }
    }

    std::string_view text_;
    std::size_t position_ = 0;
};

template<class Number>
using NumericObject = std::map<std::string, Number>;

template<class Number>
std::optional<NumericObject<Number>> parseNumericObject(std::string_view text) {
    static_assert(std::is_same_v<Number, int> || std::is_same_v<Number, double>);
    if (text.size() > 65536) return {};
    NumericObjectReader reader(text);
    NumericObject<Number> result;
    if (!reader.take('{')) return {};
    for (;;) {
        const auto key = reader.key();
        if (!key || !reader.take(':')) return {};
        const auto token = reader.number();
        if (!token) return {};
        Number value{};
        const auto parsed = std::from_chars(token->data(), token->data() + token->size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != token->data() + token->size()) return {};
        if constexpr (std::is_same_v<Number, double>) {
            if (!std::isfinite(value)) return {};
        }
        if (!result.emplace(*key, value).second || result.size() > 32) return {};
        if (reader.take('}')) break;
        if (!reader.take(',')) return {};
    }
    if (!reader.done()) return {};
    return result;
}
} // namespace triggerlab::persistence

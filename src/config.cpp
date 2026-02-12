#define TOML_IMPLEMENTATION
#include "config.h"
#include <format>

Config::FieldNotFoundError::FieldNotFoundError(std::string_view path)
    : std::runtime_error{std::format("Missing field '{}'.", path)}
{}

Config::BadTypeError::BadTypeError(std::string_view path, std::string_view expectedType)
    : std::runtime_error{std::format("'{}' is not a(n) {}.", path, expectedType)}
{}

Config::SizeMismatchError::SizeMismatchError(std::string_view path, size_t expectedLen, size_t len)
    : std::runtime_error{std::format("Array length mismatch for '{}'. Expected {} elements, got {}.", path, expectedLen, len)}
{}

Config::SignedUnsignedMismatchError::SignedUnsignedMismatchError(std::string_view path)
    : std::runtime_error{std::format("'{}': value must be positive.", path)}
{}

Config::ValueOutOfBoundsError::ValueOutOfBoundsError(
    std::string_view path,
    int64_t expectedMin,
    int64_t expectedMax,
    int64_t got
)
    : std::runtime_error{std::format("'{}': value '{}' is out of bounds [{}, {}].", path, got, expectedMin, expectedMax)}
{}

auto Config::ParseFile(const std::filesystem::path& path) -> std::expected<Config, std::string> {
    auto file = std::ifstream{path, std::ios::in | std::ios::binary};
    if (file.fail()) {
        return std::unexpected{strerror(errno)};
    }

    auto toml = toml::parse(file);
    if (!toml) {
        const auto& error = toml.error();

        return std::unexpected{
            std::format("{}\nOccurred at line {}, column {}",
                error.description(),
                error.source().begin.line,
                error.source().begin.column)
        };
    }

    return Config{std::move(toml)};
}

Config::Config(toml::table&& table)
    : m_root{std::move(table)}
{}

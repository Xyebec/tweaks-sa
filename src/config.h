#pragma once
#define TOML_EXCEPTIONS 0
#define TOML_HEADER_ONLY 0
#include <toml.hpp>
#include <boost/pfr.hpp>
#include <expected>
#include <filesystem>

template <size_t N>
struct FixedString final {
    constexpr FixedString() = default;

    constexpr FixedString(const char (&str)[N + 1]) {
        std::copy_n(str, N + 1, bytes.data());
    }

    constexpr FixedString(std::string_view str) {
        std::copy_n(str.data(), N + 1, bytes.data());
    }

    constexpr auto size() const -> size_t { return N; }
    constexpr auto empty() const -> bool { return N == 0; }

    constexpr auto data()       -> char*       { return bytes.data(); }
    constexpr auto data() const -> const char* { return bytes.data(); }

    constexpr auto operator[](size_t i)       -> char&       { return bytes[i]; }
    constexpr auto operator[](size_t i) const -> const char& { return bytes[i]; }
    
    std::array<char, N + 1> bytes{};
};

template <size_t N>
FixedString(const char (&str)[N]) -> FixedString<N - 1>;



class Config {
public:
    class FieldNotFoundError : public std::runtime_error {
    public:
        explicit FieldNotFoundError(std::string_view path);
    };

    class BadTypeError : public std::runtime_error {
    public:
        explicit BadTypeError(std::string_view path, std::string_view expectedType);
    };
    
    class SizeMismatchError : public std::runtime_error {
    public:
        explicit SizeMismatchError(std::string_view path, size_t expectedLen, size_t len);
    };
    
    class SignedUnsignedMismatchError : public std::runtime_error {
    public:
        explicit SignedUnsignedMismatchError(std::string_view path);
    };

    class ValueOutOfBoundsError : public std::runtime_error {
    public:
        explicit ValueOutOfBoundsError(
            std::string_view path,
            int64_t expectedMin,
            int64_t expectedMax,
            int64_t got
        );
    };

public:
    static auto ParseFile(const std::filesystem::path& path) -> std::expected<Config, std::string>;
    
    template <typename T>
    auto Deserialize(std::string_view key) const -> T {
        T out{};
        Deserialize(key, out);
        return out;
    }

    template <typename T>
    void Deserialize(std::string_view key, T& out) const {
        Read(key, m_root.get(key), out);
    }

private:
    explicit Config(toml::table&& table);

    static constexpr auto BUFFER_GROWTH = 32;

    template <typename T>
    static auto GetNodeAs(std::string_view path, const toml::node* node, std::string_view expectedType) {
        if (!node) {
            throw FieldNotFoundError{path};
        }

        const auto* value = node->as<T>();
        if (!value) {
            throw BadTypeError{path, expectedType};
        }

        return value;
    }

    static void Read(std::string_view path, const toml::node* node, std::string& out) {
        out = **GetNodeAs<std::string>(path, node, "string");
    }

    template <typename T>
    requires std::is_integral_v<T>
    static void Read(std::string_view path, const toml::node* node, T& out) {
        if constexpr (std::is_same_v<T, bool>) {
            out = **GetNodeAs<bool>(path, node, "boolean");
        } else {
            const auto* integer = GetNodeAs<int64_t>(path, node, "integer");
            if constexpr (std::is_unsigned_v<T>) {
                if (**integer < 0) {
                    throw SignedUnsignedMismatchError{path};
                }
            }

            using Limits = std::numeric_limits<T>;
            if (**integer < Limits::min() || **integer > Limits::max()) {
                throw ValueOutOfBoundsError{path, Limits::min(), Limits::max(), **integer};
            }

            out = static_cast<T>(**integer);
        }
    }
    
    template <typename T>
    requires std::is_floating_point_v<T>
    static void Read(std::string_view path, const toml::node* node, T& out) {
        out = static_cast<T>(**GetNodeAs<double>(path, node, "float"));
    }
    
    template <typename T>
    static void Read(std::string_view path, const toml::node* node, std::optional<T>& out) {
        if (node) {
            Read<T>(path, node, out);
        } else {
            out = std::nullopt;
        }
    }
    
    template <typename T>
    static void Read(std::string_view path, const toml::node* node, std::vector<T>& out) {
        const auto* array = GetNodeAs<toml::array>(path, node, "array");

        std::string pathBuf;
        pathBuf.reserve(path.length() + BUFFER_GROWTH);
        pathBuf = path;
        
        out.clear();
        out.reserve(array->size());

        for (size_t i = 0; i < array->size(); i++) {
            pathBuf.resize(path.length());
            pathBuf += '[';
            pathBuf += std::to_string(i);
            pathBuf += ']';

            out.emplace_back();
            Read(pathBuf, array->get(i), out[i]);
        }
    }

    template <typename T, size_t N>
    static void Read(std::string_view path, const toml::node* node, std::array<T, N>& out) {
        const auto* array = GetNodeAs<toml::array>(path, node, "array");
    
        if (array->size() != N) {
            throw SizeMismatchError{path, N, array->size()};
        }

        std::string pathBuf;
        pathBuf.reserve(path.length() + BUFFER_GROWTH);
        pathBuf = path;

        for (size_t i = 0; i < N; i++) {
            pathBuf.resize(path.length());
            pathBuf += '[';
            pathBuf += std::to_string(i);
            pathBuf += ']';

            Read(pathBuf, array->get(i), out[i]);
        }
    }

    template <typename... Ts>
    static void Read(std::string_view path, const toml::node* node, std::tuple<Ts...>& out) {
        static constexpr auto N = sizeof...(Ts);

        const auto* array = GetNodeAs<toml::array>(path, node, "array");
    
        if (array->size() != N) {
            throw SizeMismatchError{path, N, array->size()};
        }

        std::string pathBuf;
        pathBuf.reserve(path.length() + BUFFER_GROWTH);
        pathBuf = path;

        size_t index = 0;
        std::apply(
            [&](auto&&... elems) {
                ([&] {
                    pathBuf.resize(path.length());
                    pathBuf += '[';
                    pathBuf += std::to_string(index);
                    pathBuf += ']';

                    Read(pathBuf, array->get(index), elems);
                    ++index;
                }(), ...);
            },
            out
        );
    }

    template <typename T>
    requires std::is_aggregate_v<T> && std::is_class_v<T>
    static void Read(std::string_view path, const toml::node* node, T& out) {
        const auto* table = GetNodeAs<toml::table>(path, node, "table");

        std::string pathBuf;
        pathBuf.reserve(path.length() + BUFFER_GROWTH);
        pathBuf = path;

        ReadFields(pathBuf, path, out, *table, std::make_index_sequence<boost::pfr::tuple_size_v<T>>{});
    }

    template <typename T, size_t... Is>
    static void ReadFields(std::string& pathBuf, std::string_view parentPath, T& structure, const toml::table& table, std::index_sequence<Is...> /*unused*/) {
        ([&]{
            constexpr auto name = boost::pfr::get_name<Is, T>();
            static constexpr auto kebab = ToKebabCase<FixedString<name.size()>{name}>();
            constexpr auto key = std::string_view{kebab.data(), kebab.size()};

            pathBuf.resize(parentPath.length());
            pathBuf += '.';
            pathBuf += key;

            Read(pathBuf, table.get(key), boost::pfr::get<Is>(structure));
        }(), ...);
    }

private:
    static constexpr auto IsUpper(char ch) -> bool {
        return ch >= 'A' && ch <= 'Z';
    }

    static constexpr auto IsLower(char ch) -> bool {
        return ch >= 'a' && ch <= 'z';
    }

    static constexpr auto ToLower(char ch) -> char {
        return IsLower(ch) ? ch : static_cast<char>(ch + ('a' - 'A'));
    }

    template <size_t N>
    static consteval auto ToKebabCaseLen(const FixedString<N>& str) -> size_t {
        if (str.empty()) {
            return 0;
        }
    
        size_t length = 1;
    
        for (size_t i = 1; i < str.size(); i++) {
            const auto c = str[i];
            if (IsUpper(c) && (IsLower(str[i - 1]) || (i + 1 < str.size() && IsLower(str[i + 1])))) {
                length += 1; // Hyphen
            }
            
            length += 1; // Character
        }
        
        return length;
    }

    template <FixedString str>
    static consteval auto ToKebabCase() {
        constexpr auto kebabLen = ToKebabCaseLen(str);
    
        FixedString<kebabLen> kebab;
        if (kebabLen == 0) {
            return kebab;
        }

        size_t offset = 0;
    
        kebab[offset++] += ToLower(str[0]);

        for (size_t i = 1; i < str.size(); i++) {
            const auto c = str[i];
            if (IsUpper(c) && (IsLower(str[i - 1]) || (i + 1 < str.size() && IsLower(str[i + 1])))) {
                kebab[offset++] += '-';
            }
        
            kebab[offset++] += c == '_' ? '-' : ToLower(c);
        }

        return kebab;
    }

private:
    toml::table m_root;
};

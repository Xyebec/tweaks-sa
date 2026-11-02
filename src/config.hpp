#pragma once
#include <ranges>
#include <toml++/toml.hpp>
#include <boost/pfr.hpp>
#include <expected>
#include <filesystem>
#include <magic_enum/magic_enum.hpp>
#include <stdexcept>
#include <type_traits>
#include <variant>

template <size_t N>
struct FixedString final {
    constexpr FixedString() noexcept = default;

    constexpr FixedString(const char (&str)[N + 1]) {
        std::copy_n(str, N + 1, bytes.data());
    }

    constexpr FixedString(std::string_view str) {
        std::copy_n(str.data(), N + 1, bytes.data());
    }

    constexpr auto size() const noexcept -> size_t { return N; }
    constexpr auto empty() const noexcept -> bool { return N == 0; }

    constexpr auto data()       noexcept -> char*       { return bytes.data(); }
    constexpr auto data() const noexcept -> const char* { return bytes.data(); }

    constexpr auto operator[](size_t i)       -> char&       { return bytes[i]; }
    constexpr auto operator[](size_t i) const -> const char& { return bytes[i]; }

    constexpr auto view() const noexcept -> std::string_view { return std::string_view{bytes.data(), N}; }

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

    class InvalidEnumValue : public std::runtime_error {
    public:
        explicit InvalidEnumValue(std::string_view path, int64_t value);
        explicit InvalidEnumValue(std::string_view path, std::string_view value);
    };

    struct PathNode {
        const PathNode* parent = nullptr;
        std::variant<std::string_view, size_t> segment;

        auto ToString() const -> std::string;
    };

    struct Context {
        PathNode path;
        const toml::node* node;

        template <typename T>
        const auto& GetNodeAs() const {
            if (node == nullptr) {
                throw FieldNotFoundError{path.ToString()};
            }

            const auto* value = node->as<T>();
            if (value == nullptr) {
                throw BadTypeError{path.ToString(), TypeToString<T>()};
            }

            return *value;
        }

        auto Subcontext(std::string_view key, const toml::node* node) const -> Context {
            return Context{PathNode{&path, key}, node};
        }
        
        auto Subcontext(size_t index, const toml::node* node) const -> Context {
            return Context{PathNode{&path, index}, node};
        }

        auto GetTableChildren() const {
            if (node == nullptr) { throw FieldNotFoundError{path.ToString()}; }
            const auto* tbl = node->as_table();
            if (tbl == nullptr) { throw BadTypeError{path.ToString(), "table"}; }

            return *tbl | std::ranges::views::transform([this](const auto& pair) {
                const auto keyView = std::string_view{pair.first.data(), pair.first.length()};
                return std::pair<std::string_view, Context>{
                    keyView, 
                    this->Subcontext(keyView, &pair.second)
                };
            });
        }
    };

    template <typename T>
    struct De {
        static auto Deserialize(const Context& ctx) -> T = delete;
    };

public:
    static auto ParseFile(const std::filesystem::path& path) -> std::expected<Config, std::string>;
    
    template <typename T>
    auto Deserialize(std::string_view key) const -> T {
        return De<T>::Deserialize({ PathNode{nullptr, key}, m_root.at_path(key).node() });
    }
    
    template <typename T>
    void Deserialize(std::string_view key, T& out) const {
        out = Deserialize<T>(key);
    }

private:
    explicit Config(toml::table&& table) noexcept;

private:
    template <FixedString str>
    static consteval auto ToKebabCase() {
        static constexpr auto IsUpper = [](char ch) { return ch >= 'A' && ch <= 'Z'; };
        static constexpr auto IsLower = [](char ch) { return ch >= 'a' && ch <= 'z'; };
        static constexpr auto ToLower = [](char ch) { return IsUpper(ch) ? static_cast<char>(ch + ('a' - 'A')) : ch; };

        static_assert(!str.empty(), "str is empty");

        constexpr auto KEBAB_LEN = []() {
            size_t length = 1;
            for (size_t i = 1; i < str.size(); i++) {
                if (IsUpper(str[i]) && (IsLower(str[i - 1]) || (i + 1 < str.size() && IsLower(str[i + 1])))) {
                    length += 1; // Hyphen
                }
                length += 1; // Character
            }
            return length;
        }();
        
        auto kebab = FixedString<KEBAB_LEN>{};
        auto offset = size_t{0};

        kebab[offset++] = ToLower(str[0]);

        for (size_t i = 1; i < str.size(); i++) {
            const auto c = str[i];
            if (IsUpper(c) && (IsLower(str[i - 1]) || (i + 1 < str.size() && IsLower(str[i + 1])))) {
                kebab[offset++] = '-';
            }
            kebab[offset++] = c == '_' ? '-' : ToLower(c);
        }

        return kebab;
    }

    template <typename T>
    static constexpr std::string_view TypeToString() = delete;

private:
    toml::table m_root;
};

template <> constexpr std::string_view Config::TypeToString<std::string>()     { return "string"; }
template <> constexpr std::string_view Config::TypeToString<int64_t>()         { return "integer"; }
template <> constexpr std::string_view Config::TypeToString<double>()          { return "float"; }
template <> constexpr std::string_view Config::TypeToString<bool>()            { return "boolean"; }
template <> constexpr std::string_view Config::TypeToString<toml::date>()      { return "date"; }
template <> constexpr std::string_view Config::TypeToString<toml::time>()      { return "time"; }
template <> constexpr std::string_view Config::TypeToString<toml::date_time>() { return "datetime"; }
template <> constexpr std::string_view Config::TypeToString<toml::array>()     { return "array"; }
template <> constexpr std::string_view Config::TypeToString<toml::table>()     { return "table"; }

template <>
struct Config::De<std::string> {
    static auto Deserialize(const Context& ctx) -> std::string {
        return *ctx.GetNodeAs<std::string>();
    }
};

template <typename T>
requires std::is_integral_v<T>
struct Config::De<T> {
    static auto Deserialize(const Context& ctx) -> T {
        if constexpr (std::is_same_v<T, bool>) {
            return *ctx.GetNodeAs<bool>();
        } else {
            const auto integer = *ctx.GetNodeAs<int64_t>();
            if constexpr (std::is_unsigned_v<T>) {
                if (integer < 0) {
                    throw SignedUnsignedMismatchError{ctx.path.ToString()};
                }
            }

            using Limits = std::numeric_limits<T>;
            if (integer < Limits::min() || integer > Limits::max()) {
                throw ValueOutOfBoundsError{ctx.path.ToString(), Limits::min(), Limits::max(), integer};
            }

            return static_cast<T>(integer);
        }
    }
};

template <typename T>
requires std::is_floating_point_v<T>
struct Config::De<T> {
    static auto Deserialize(const Context& ctx) -> T {
        return static_cast<T>(*ctx.GetNodeAs<double>());
    }
};

template <typename T>
struct Config::De<std::optional<T>> {
    static auto Deserialize(const Context& ctx) -> std::optional<T> {
        if (ctx.node != nullptr) {
            return De<T>::Deserialize(ctx);
        } else {
            return std::nullopt;
        }
    }
};

template <typename T>
struct Config::De<std::vector<T>> {
    static auto Deserialize(const Context& ctx) -> std::vector<T> {
        const auto& array = ctx.GetNodeAs<toml::array>();
        
        auto out = std::vector<T>{};
        out.reserve(array.size());

        for (const auto& [i, element] : std::views::enumerate(array)) {
            out.emplace_back(De<T>::Deserialize(ctx.Subcontext(i, &element)));
        }

        return out;
    }
};

template <typename T, size_t N>
struct Config::De<std::array<T, N>> {
    static auto Deserialize(const Context& ctx) -> std::array<T, N> {
        const auto& array = ctx.GetNodeAs<toml::array>();
    
        if (array.size() != N) {
            throw SizeMismatchError{ctx.path.ToString(), N, array.size()};
        }

        auto out = std::array<T, N>{};

        for (const auto& [i, element] : std::views::enumerate(array)) {
            out[i] = De<T>::Deserialize(ctx.Subcontext(i, &element));
        }

        return out;
    }
};

template <typename... Ts>
struct Config::De<std::tuple<Ts...>> {
    static auto Deserialize(const Context& ctx) -> std::tuple<Ts...> {
        static constexpr auto N = sizeof...(Ts);

        const auto& array = ctx.GetNodeAs<toml::array>();
    
        if (array.size() != N) {
            throw SizeMismatchError{ctx.path.ToString(), N, array.size()};
        }

        auto out = std::tuple<Ts...>{};
        auto index = size_t{0};
        std::apply(
            [&](auto&&... elems) {
                ([&] {
                    using T = std::remove_cvref_t<decltype(elems)>;
                    elems = De<T>::Deserialize(ctx.Subcontext(index, array.get(index)));
                    ++index;
                }(), ...);
            },
            out
        );

        return out;
    }
};

template <typename T, typename U>
struct Config::De<std::pair<T, U>> {
    static auto Deserialize(const Context& ctx) -> std::pair<T, U> {
        const auto& array = ctx.GetNodeAs<toml::array>();
        return std::pair<T, U>{
            De<T>::Deserialize(ctx.Subcontext(0, array.get(0))),
            De<U>::Deserialize(ctx.Subcontext(1, array.get(1))),
        };
    }
};

template <typename T>
requires std::is_enum_v<T>
struct Config::De<T> {
    static auto Deserialize(const Context& ctx) -> T {
        auto value = De<std::underlying_type_t<T>>::Deserialize(ctx);
        auto enumeration = magic_enum::enum_cast<T>(value);
        if (!enumeration.has_value()) {
            throw InvalidEnumValue{ctx.path.ToString(), value};
        }
        return *enumeration;
    }
};

template <typename T>
requires std::is_aggregate_v<T> && std::is_class_v<T>
struct Config::De<T> {
    static auto Deserialize(const Context& ctx) -> T {
        const auto& table = ctx.GetNodeAs<toml::table>();

        auto out = T{};
        DeserializeFields(ctx, out, table, std::make_index_sequence<boost::pfr::tuple_size_v<T>>{});
        return out;
    }

    template <size_t... Is>
    static void DeserializeFields(const Context& ctx, T& structure, const toml::table& table, std::index_sequence<Is...> /*unused*/) {
        ([&]{
            static constexpr auto name = boost::pfr::get_name<Is, T>();
            static constexpr auto kebab = ToKebabCase<FixedString<name.size()>{name}>();
            static constexpr auto key = kebab.view();

            auto& out = boost::pfr::get<Is>(structure);
            using FieldT = std::remove_cvref_t<decltype(out)>;
            out = De<FieldT>::Deserialize(ctx.Subcontext(key, table.get(key)));
        }(), ...);
    }
};

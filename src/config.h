#pragma once
#define TOML_EXCEPTIONS 0
#define TOML_HEADER_ONLY 0
#include <toml.hpp>
#include <boost/pfr.hpp>
#include <expected>
#include <filesystem>
#include <memory_resource>

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

    struct Context {
        const std::pmr::polymorphic_allocator<char>& alloc; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
        std::string_view path;
        const toml::node* node;
    };

    template <typename T>
    struct De {
        static auto Deserialize(const Context& ctx) -> T = delete;
    };

public:
    static auto ParseFile(const std::filesystem::path& path) -> std::expected<Config, std::string>;
    
    template <typename T>
    auto Deserialize(std::string_view key) const -> T {
        char buf[4096];
        auto arena = std::pmr::monotonic_buffer_resource{buf, sizeof(buf)};

        const auto view = m_root.at_path(key);
        return De<T>::Deserialize({ &arena, key, view.node() });
    }
    
    template <typename T>
    void Deserialize(std::string_view key, T& out) const {
        out = Deserialize<T>(key);
    }

private:
    explicit Config(toml::table&& table) noexcept;

    template <typename T>
    static auto GetNodeAs(std::string_view path, const toml::node* node, std::string_view expectedType) {
        if (node == nullptr) {
            throw FieldNotFoundError{path};
        }

        const auto* value = node->as<T>();
        if (value == nullptr) {
            throw BadTypeError{path, expectedType};
        }

        return value;
    }

private:
    template <FixedString str>
    static consteval auto ToKebabCase() {
        static constexpr auto IsUpper = [](char ch) { return ch >= 'A' && ch <= 'Z'; };
        static constexpr auto IsLower = [](char ch) { return ch >= 'a' && ch <= 'z'; };
        static constexpr auto ToLower = [](char ch) { return IsLower(ch) ? ch : static_cast<char>(ch + ('a' - 'A')); };

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

    static auto CreatePathBuf(const std::pmr::polymorphic_allocator<char>& alloc, std::string_view basePath) -> std::pmr::string {
        auto pathBuf = std::pmr::string{alloc};
        pathBuf.reserve(basePath.length() + 32);
        pathBuf = basePath;
        return pathBuf;
    };

    static void AppendIndex(std::pmr::string& pathBuf, size_t index) {
        pathBuf += '[';

        char buf[std::numeric_limits<size_t>::digits10 + 1];
        if (const auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), index); ec == std::errc{}) {
            pathBuf.append(buf, ptr - buf);
        } else {
            pathBuf += '?';
        }

        pathBuf += ']';
    }

private:
    toml::table m_root;
};

template <>
struct Config::De<std::string> {
    static auto Deserialize(const Context& ctx) -> std::string {
        return **GetNodeAs<std::string>(ctx.path, ctx.node, "string");
    }
};

template <typename T>
requires std::is_integral_v<T>
struct Config::De<T> {
    static auto Deserialize(const Context& ctx) -> T {
        if constexpr (std::is_same_v<T, bool>) {
            return **GetNodeAs<bool>(ctx.path, ctx.node, "boolean");
        } else {
            const auto* integer = GetNodeAs<int64_t>(ctx.path, ctx.node, "integer");
            if constexpr (std::is_unsigned_v<T>) {
                if (**integer < 0) {
                    throw SignedUnsignedMismatchError{ctx.path};
                }
            }

            using Limits = std::numeric_limits<T>;
            if (**integer < Limits::min() || **integer > Limits::max()) {
                throw ValueOutOfBoundsError{ctx.path, Limits::min(), Limits::max(), **integer};
            }

            return static_cast<T>(**integer);
        }
    }
};

template <typename T>
requires std::is_floating_point_v<T>
struct Config::De<T> {
    static auto Deserialize(const Context& ctx) -> T {
        return static_cast<T>(**GetNodeAs<double>(ctx.path, ctx.node, "float"));
    }
};

template<typename T>
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
        const auto* array = GetNodeAs<toml::array>(ctx.path, ctx.node, "array");

        auto pathBuf = CreatePathBuf(ctx.alloc, ctx.path);
        
        auto out = std::vector<T>{};
        out.reserve(array->size());

        for (size_t i = 0; i < array->size(); i++) {
            pathBuf.resize(ctx.path.length());
            AppendIndex(pathBuf, i);

            out.emplace_back(De<T>::Deserialize({ ctx.alloc, pathBuf, array->get(i) }));
        }

        return out;
    }
};

template <typename T, size_t N>
struct Config::De<std::array<T, N>> {
    static auto Deserialize(const Context& ctx) -> std::array<T, N> {
        const auto* array = GetNodeAs<toml::array>(ctx.path, ctx.node, "array");
    
        if (array->size() != N) {
            throw SizeMismatchError{ctx.path, N, array->size()};
        }

        auto pathBuf = CreatePathBuf(ctx.alloc, ctx.path);

        auto out = std::array<T, N>{};

        for (size_t i = 0; i < N; i++) {
            pathBuf.resize(ctx.path.length());
            AppendIndex(pathBuf, i);

            out[i] = De<T>::Deserialize({ ctx.alloc, pathBuf, array->get(i) });
        }

        return out;
    }
};

template <typename... Ts>
struct Config::De<std::tuple<Ts...>> {
    static auto Deserialize(const Context& ctx) -> std::tuple<Ts...> {
        static constexpr auto N = sizeof...(Ts);

        const auto* array = GetNodeAs<toml::array>(ctx.path, ctx.node, "array");
    
        if (array->size() != N) {
            throw SizeMismatchError{ctx.path, N, array->size()};
        }

        auto pathBuf = CreatePathBuf(ctx.alloc, ctx.path);

        auto out = std::tuple<Ts...>{};
        auto index = size_t{0};
        std::apply(
            [&](auto&&... elems) {
                ([&] {
                    pathBuf.resize(ctx.path.length());
                    AppendIndex(pathBuf, index);

                    using T = std::remove_cvref_t<decltype(elems)>;
                    elems = De<T>::Deserialize({ ctx.alloc, pathBuf, array->get(index) });
                    ++index;
                }(), ...);
            },
            out
        );

        return out;
    }
};

template <typename T>
requires std::is_aggregate_v<T> && std::is_class_v<T>
struct Config::De<T> {
    static auto Deserialize(const Context& ctx) -> T {
        const auto* table = GetNodeAs<toml::table>(ctx.path, ctx.node, "table");

        auto pathBuf = CreatePathBuf(ctx.alloc, ctx.path);
        pathBuf += '.';
        const auto baseLen = pathBuf.length();

        auto out = T{};
        DeserializeFields(pathBuf, baseLen, out, *table, std::make_index_sequence<boost::pfr::tuple_size_v<T>>{});
        return out;
    }

    template <size_t... Is>
    static void DeserializeFields(std::pmr::string& pathBuf, size_t baseLen, T& structure, const toml::table& table, std::index_sequence<Is...> /*unused*/) {
        ([&]{
            constexpr auto name = boost::pfr::get_name<Is, T>();
            static constexpr auto kebab = ToKebabCase<FixedString<name.size()>{name}>();
            static constexpr auto key = kebab.view();

            pathBuf.resize(baseLen);
            pathBuf += key;

            auto& out = boost::pfr::get<Is>(structure);
            using FieldT = std::remove_cvref_t<decltype(out)>;
            
            out = De<FieldT>::Deserialize({ pathBuf.get_allocator(), pathBuf, table.get(key) });
        }(), ...);
    }
};

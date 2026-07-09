#ifndef OPENMW_COMPONENTS_MISC_SPAN_HPP
#define OPENMW_COMPONENTS_MISC_SPAN_HPP

// std::span polyfill for GCC 10 / C++17.
// On compilers with C++20 std::span, this just includes <span>.

#if __has_include(<span>) && __cplusplus >= 202002L
#include <span>
#else

#include <cstddef>
#include <type_traits>
#include <array>
#include <vector>

namespace std
{
    inline constexpr std::size_t dynamic_extent = static_cast<std::size_t>(-1);

    template <typename T, std::size_t Extent = dynamic_extent>
    class span
    {
    public:
        using element_type = T;
        using value_type = std::remove_cv_t<T>;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using const_pointer = const T*;
        using reference = T&;
        using const_reference = const T&;
        using iterator = pointer;

        constexpr span() noexcept : mData(nullptr), mSize(0) {}
        constexpr span(pointer data, size_type size) noexcept : mData(data), mSize(size) {}
        constexpr span(pointer first, pointer last) noexcept : mData(first), mSize(last - first) {}

        template <std::size_t N>
        constexpr span(T (&arr)[N]) noexcept : mData(arr), mSize(N) {}

        template <std::size_t N>
        constexpr span(std::array<value_type, N>& arr) noexcept : mData(arr.data()), mSize(N) {}

        template <std::size_t N>
        constexpr span(const std::array<value_type, N>& arr) noexcept : mData(arr.data()), mSize(N) {}

        template <typename Container,
            typename = std::enable_if_t<!std::is_array_v<Container>
                && std::is_convertible_v<typename Container::value_type(*)[], T(*)[]>>>
        constexpr span(Container& c) noexcept : mData(c.data()), mSize(c.size()) {}

        template <typename Container,
            typename = std::enable_if_t<!std::is_array_v<Container>
                && std::is_convertible_v<typename Container::value_type(*)[], T(*)[]>>>
        constexpr span(const Container& c) noexcept : mData(c.data()), mSize(c.size()) {}

        template <typename U, std::size_t N>
        constexpr span(const span<U, N>& other) noexcept : mData(other.data()), mSize(other.size()) {}

        constexpr pointer data() const noexcept { return mData; }
        constexpr size_type size() const noexcept { return mSize; }
        constexpr bool empty() const noexcept { return mSize == 0; }
        constexpr reference operator[](size_type idx) const { return mData[idx]; }
        constexpr reference front() const { return mData[0]; }
        constexpr reference back() const { return mData[mSize - 1]; }
        constexpr iterator begin() const noexcept { return mData; }
        constexpr iterator end() const noexcept { return mData + mSize; }
        constexpr size_type size_bytes() const noexcept { return mSize * sizeof(T); }

        constexpr span<T, dynamic_extent> subspan(size_type offset, size_type count = dynamic_extent) const
        {
            return span<T, dynamic_extent>(mData + offset, count == dynamic_extent ? mSize - offset : count);
        }

    private:
        pointer mData;
        size_type mSize;
    };

    // Deduction guides
    template <typename T, std::size_t N>
    span(T (&)[N]) -> span<T, N>;

    template <typename T, std::size_t N>
    span(std::array<T, N>&) -> span<T, N>;

    template <typename T, std::size_t N>
    span(const std::array<T, N>&) -> span<const T, N>;

    template <typename Container>
    span(Container&) -> span<typename Container::value_type>;

    template <typename Container>
    span(const Container&) -> span<const typename Container::value_type>;
}

#endif // __has_include(<span>)

#endif // OPENMW_COMPONENTS_MISC_SPAN_HPP

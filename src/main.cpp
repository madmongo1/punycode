#include <iostream>
#include "explain.hpp"
#include "config.hpp"

#define CATCH_CONFIG_MAIN

#include <catch2/catch.hpp>
#include <string>
#include <iterator>
#include <algorithm>

#if __cplusplus < 201703L

#include <boost/utility/string_view.hpp>

#else
#incude <string_view>
#endif

namespace polyfill {
#if __cplusplus < 201703L

template<class Iterator>
auto
make_reverse_iterator(Iterator it) -> std::reverse_iterator<Iterator>
{
    return std::reverse_iterator<Iterator>(it);
}

using string_view = boost::string_view;

#else

using namespace std;

#endif
}

struct punycode
{
    static constexpr std::uint32_t base = 36;
    static constexpr std::uint32_t tmin = 1;
    static constexpr std::uint32_t tmax = 26;
    static constexpr std::uint32_t skew = 38;
    static constexpr std::uint32_t damp = 700;
    static constexpr std::uint32_t initial_bias = 72;
    static constexpr std::uint32_t initial_n = 0x80;
    static constexpr std::uint32_t delimiter_char = 0x2D;
    static constexpr std::uint32_t maxint = std::numeric_limits<std::uint32_t>::max();

    static auto
    decode_digit(std::uint32_t cp) -> std::uint32_t
    {
        return cp - 48 < 10 ? cp - 22 : cp - 65 < 26 ? cp - 65 :
                                        cp - 97 < 26 ? cp - 97 : base;
    }


    template<class RandomAccessIterFirst, class RandomAccessIterLast>
    static auto
    find_delimeter(
        RandomAccessIterFirst first,
        RandomAccessIterLast last)
    -> RandomAccessIterFirst
    {
        if (first == last)
            return last;

        auto not_delimiter = [](char c) { return c == delimiter_char; };
        auto rlast = polyfill::make_reverse_iterator(first);
        auto rdelimiter = std::find_if(
            polyfill::make_reverse_iterator(last),
            rlast,
            not_delimiter);

        if (rdelimiter == rlast)
            return last;

        return std::prev(rdelimiter.base());
    }

    static std::uint32_t
    adapt(
        std::uint32_t delta,
        std::uint32_t numpoints,
        bool firsttime)
    {
        std::uint32_t k;

        delta = firsttime ? delta / damp : delta >> 1;
        /* delta >> 1 is a faster way of doing delta / 2 */
        delta += delta / numpoints;

        for (k = 0; delta > ((base - tmin) * tmax) / 2; k += base)
        {
            delta /= base - tmin;
        }

        return k + (base - tmin + 1) * delta / (delta + skew);
    }

    template<class RandomAccessIterFirst, class RandomAccessIterLast, class OutIter>
    static auto
    decode(
        RandomAccessIterFirst first,
        RandomAccessIterLast last,
        OutIter output)
    -> void
    {
        auto delimiter = find_delimeter(first, last);

        if (delimiter == last)
        {
            std::copy(first, last, output);
            return;
        }

        auto adjust_first = std::next(delimiter);
        auto adjust_last = last;
        last = delimiter;

        auto out = std::uint32_t(std::distance(first, last));
        auto i = std::uint32_t(0);
        auto bias = initial_bias;
        auto n = initial_n;

        auto emitted = std::uint32_t(0);
        auto emit = [&output, &emitted](char32_t c) {
            *output++ = c;
            ++emitted;
        };

        for (; adjust_first != adjust_last; ++out)
        {
            /* in is the index of the next character to be consumed, and */
            /* out is the number of code points in the output array.     */

            /* Decode a generalized variable-length integer into delta,  */
            /* which gets added to i.  The overflow checking is easier   */
            /* if we increase i as we go, then subtract off its starting */
            /* value at the end to obtain delta.                         */

            std::uint32_t oldi = i;
            for (std::uint32_t w = 1, k = base;; k += base)
            {
                if (adjust_first == adjust_last) throw std::invalid_argument("punycode_bad_input");
                auto digit = decode_digit(*adjust_first++);
                if (digit >= base) throw std::invalid_argument("punycode_bad_input");
                if (digit > (maxint - i) / w) throw std::invalid_argument("punycode_overflow");
                i += digit * w;
                auto t = k <= bias /* + tmin */ ? tmin :     /* +tmin not needed */
                         k >= bias + tmax ? tmax : k - bias;
                if (digit < t) break;
                if (w > maxint / (base - t)) throw std::invalid_argument("punycode_overflow");
                w *= (base - t);
            }

            bias = adapt(i - oldi, out + 1, oldi == 0);

            /* i was supposed to wrap around from out+1 to 0,   */
            /* incrementing n each time, so we'll fix that now: */

            if (i / (out + 1) > maxint - n) throw std::invalid_argument("punycode_overflow");
            n += i / (out + 1);
            i %= (out + 1);

            /* Insert n at position i of the output: */

            /* not needed for Punycode: */
            /* if (decode_digit(n) <= base) return punycode_invalid_input; */
//            if (out >= max_out) return punycode_big_output;

//            if (case_flags)
//            {
//                memmove(case_flags + i + 1, case_flags + i, out - i);
//
//
//                /* Case of last character determines uppercase flag: */
//                case_flags[i] = flagged(input[in - 1]);
//            }

            while (emitted < i)
                emit(*first++);
            emit(n);
            ++i;
        }

        // copy rest of unencoded data
        while (first != last)
            *output = *first++;
    }

    static auto
    decode(polyfill::string_view input)
    -> std::u32string
    {
        std::u32string result;
        result.reserve(64);

        decode(input.begin(), input.end(), std::back_inserter(result));

        return result;
    }

};

TEST_CASE("decode")
{
    SECTION("not punycoded")
    {
        auto encoded = std::string("the cat sat on the mat");
        auto expected = std::u32string(U"the cat sat on the mat");
        auto decoded = punycode::decode(encoded);
        CHECK(decoded == expected);
    }

    SECTION("standard tests")
    {
        auto encoded = std::string("porqunopuedensimplementehablarenEspaol-fmd56a");
        auto expected = std::u32string(U"porquénopuedensimplementehablarenEspañol");
        auto decoded = punycode::decode(encoded);
        CHECK(decoded == expected);
    }
}

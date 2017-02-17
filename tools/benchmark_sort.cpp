/***************************************************************************
 *  tools/benchmark_sort.cpp
 *
 *  Part of the STXXL. See http://stxxl.org
 *
 *  Copyright (C) 2013 Timo Bingmann <tb@panthema.net>
 *
 *  Distributed under the Boost Software License, Version 1.0.
 *  (See accompanying file LICENSE_1_0.txt or copy at
 *  http://www.boost.org/LICENSE_1_0.txt)
 **************************************************************************/

/*
 * This program will benchmark the different sorting methods provided by STXXL
 * using three different data types: first a pair of 32-bit uints, "then a pair
 * 64-bit uint and then a larger structure of 64 bytes.
 */

#include <stxxl/bits/common/tuple.h>
#include <stxxl/cmdline>
#include <stxxl/ksort>
#include <stxxl/sort>
#include <stxxl/stream>
#include <stxxl/vector>

#include <cstdint>
#include <limits>

using stxxl::timestamp;
using stxxl::external_size_type;

#define MB (1024 * 1024)

// pair of uint32_t = 8 bytes
typedef stxxl::tuple<uint32_t, uint32_t> pair32_type;

// pair of uint64_t = 16 bytes
typedef stxxl::tuple<uint64_t, uint64_t> pair64_type;

// larger struct of 64 bytes
struct struct64_type : public pair64_type
{
    char junk[16 + 32];

    struct64_type() { }

    explicit struct64_type(const pair64_type& pt)
        : pair64_type(pt)
    { }
};

// construct a simple sorting benchmark for the value type
template <typename ValueType, typename RandomGenerator>
class BenchmarkSort
{
    typedef ValueType value_type;

    struct value_less
    {
        bool operator () (const value_type& a, const value_type& b) const
        {
            return a.first < b.first;
        }

        static value_type min_value()
        { return value_type(value_type::min_value()); }

        static value_type max_value()
        { return value_type(value_type::max_value()); }
    };

    struct value_key_second
    {
        typedef typename value_type::second_type key_type;

        key_type operator () (const value_type& p) const
        { return p.second; }

        static value_type min_value()
        { return value_type(value_type::min_value()); }

        static value_type max_value()
        { return value_type(value_type::max_value()); }
    };

    struct random_stream
    {
        typedef ValueType value_type;

        RandomGenerator m_rng;

        value_type m_value;

        external_size_type m_counter;

        explicit random_stream(external_size_type size)
            : m_counter(size)
        {
            m_value.first = m_rng();
            m_value.second = m_rng();
        }

        const value_type& operator * () const
        {
            return m_value;
        }

        random_stream& operator ++ ()
        {
            assert(m_counter > 0);
            --m_counter;

            m_value.first = m_rng();
            m_value.second = m_rng();
            return *this;
        }

        bool empty() const
        {
            return (m_counter == 0);
        }
    };

    static void output_result(double elapsed, external_size_type vec_size)
    {
        std::cout << "finished in " << elapsed << " seconds @ "
                  << ((double)vec_size * sizeof(value_type) / MB / elapsed)
                  << " MiB/s" << std::endl;
    }

public:
    BenchmarkSort(const char* desc, external_size_type length, size_t memsize)
    {
        // construct vector
        external_size_type vec_size = stxxl::div_ceil(length, sizeof(ValueType));

        stxxl::vector<ValueType> vec(vec_size);

        // construct random stream

        std::cout << "#!!! running sorting test with " << desc << " = " << sizeof(ValueType) << " bytes."
                  << std::endl;
        {
            std::cout << "# materialize random_stream into vector of size " << vec.size() << std::endl;
            double ts1 = timestamp();

            random_stream rs(vec_size);
            stxxl::stream::materialize(rs, vec.begin(), vec.end());

            double elapsed = timestamp() - ts1;
            output_result(elapsed, vec_size);
        }
        {
            std::cout << "# stxxl::sort vector of size " << vec.size() << std::endl;
            double ts1 = timestamp();

            stxxl::sort(vec.begin(), vec.end(), value_less(), memsize);

            double elapsed = timestamp() - ts1;
            output_result(elapsed, vec_size);
        }
        {
            std::cout << "# stxxl::ksort vector of size " << vec.size() << std::endl;
            double ts1 = timestamp();

            stxxl::ksort(vec.begin(), vec.end(), value_key_second(), memsize);

            double elapsed = timestamp() - ts1;
            output_result(elapsed, vec_size);
        }
        vec.clear();

        {
            std::cout << "# stxxl::stream::sort of size " << vec_size << std::endl;
            double ts1 = timestamp();

            typedef stxxl::stream::sort<random_stream, value_less>
                random_stream_sort_type;

            random_stream stream(vec_size);
            random_stream_sort_type stream_sort(stream, value_less(), memsize);

            stxxl::stream::discard(stream_sort);

            double elapsed = timestamp() - ts1;
            output_result(elapsed, vec_size);
        }

        std::cout << std::endl;
    }
};

// run sorting benchmark for the three types defined above.
int benchmark_sort(int argc, char* argv[])
{
    // parse command line
    stxxl::cmdline_parser cp;

    cp.set_description(
        "This program will benchmark the different sorting methods provided by "
        "STXXL using three different data types: first a pair of 32-bit uints, "
        "then a pair 64-bit uint and then a larger structure of 64 bytes.");
    cp.set_author("Timo Bingmann <tb@panthema.net>");

    external_size_type length = 0;
    cp.add_param_bytes("size", length,
                       "Amount of data to sort (e.g. 1GiB)");

    size_t memsize = 256 * MB;
    cp.add_bytes('M', "ram", memsize,
                 "Amount of RAM to use when sorting, default: 256 MiB");

    if (!cp.process(argc, argv))
        return -1;

    BenchmarkSort<pair32_type, stxxl::random_number32>(
        "pair of uint32_t", length, memsize);

    BenchmarkSort<pair64_type, stxxl::random_number32>(
        "pair of uint64_t", length, memsize);

    BenchmarkSort<struct64_type, stxxl::random_number32>(
        "struct of 64 bytes", length, memsize);

    return 0;
}

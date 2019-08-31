/***************************************************************************
 *  tests/stream/test_range.cpp
 *
 *  Part of the STXXL. See http://stxxl.org
 *
 *  Copyright (C) 2018 Michael Hamann <michael.hamann@kit.edu>
 *
 *  Distributed under the Boost Software License, Version 1.0.
 *  (See accompanying file LICENSE_1_0.txt or copy at
 *  http://www.boost.org/LICENSE_1_0.txt)
 **************************************************************************/

#include <algorithm>
#include <cstdint>
#include <random>

#include <tlx/die.hpp>

#include <stxxl/stream>

int main()
{
    constexpr size_t num_values = 1024;
    using value_type = uint64_t;

    std::vector<value_type> values;
    values.reserve(num_values);

    for (size_t i = 0; i < num_values; ++i) {
        values.push_back(i);
    }

    auto stream = stxxl::stream::streamify(values.begin(), values.end());

    size_t i = 0;
    for (size_t v : stxxl::stream::range(stream))
    {
        die_unequal(i, v);
        ++i;
    }

    die_unequal(i, num_values);

    return 0;
}

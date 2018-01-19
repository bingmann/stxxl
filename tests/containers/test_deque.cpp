/***************************************************************************
 *  tests/containers/test_deque.cpp
 *
 *  Part of the STXXL. See http://stxxl.org
 *
 *  Copyright (C) 2006 Roman Dementiev <dementiev@ira.uka.de>
 *  Copyright (C) 2009 Andreas Beckmann <beckmann@cs.uni-frankfurt.de>
 *
 *  Distributed under the Boost Software License, Version 1.0.
 *  (See accompanying file LICENSE_1_0.txt or copy at
 *  http://www.boost.org/LICENSE_1_0.txt)
 **************************************************************************/

#define STXXL_DEFAULT_BLOCK_SIZE(T) 4096

#include <iostream>
#include <iterator>

#include <tlx/die.hpp>
#include <tlx/logger.hpp>

#include <stxxl/deque>

int main(int argc, char* argv[])
{
    if (argc != 2) {
        LOG1 << "Usage: " << argv[0] << " #ops";
        return -1;
    }

    stxxl::deque<int> Deque;

    stxxl::deque<int>::const_iterator b = Deque.begin();
    stxxl::deque<int>::const_iterator e = Deque.end();
    die_unless(b == e);
    Deque.emplace_front(4);
    Deque.push_front(3);
    Deque.push_front(2);
    Deque.push_front(1);
    b = Deque.begin();
    die_unless(b != e);
    Deque.push_back(5);
    Deque.emplace_back(6);
    std::copy(Deque.begin(), Deque.end(), std::ostream_iterator<int>(std::cout, " "));
    die_unless(stxxl::is_sorted(Deque.begin(), Deque.end()));
    die_unless(Deque.size() == 6);

    stxxl::random_number32 rand;
    stxxl::deque<int> XXLDeque;
    std::deque<int> STDDeque;

    uint64_t ops = foxxll::atouint64(argv[1]);
    for (uint64_t i = 0; i < ops; ++i)
    {
        unsigned curOP = rand() % 6;
        unsigned value = rand();
        switch (curOP)
        {
        case 0:
        case 1:
            XXLDeque.push_front(value);
            STDDeque.push_front(value);
            break;
        case 2:
            XXLDeque.push_back(value);
            STDDeque.push_back(value);
            break;
        case 3:
            if (!XXLDeque.empty())
            {
                XXLDeque.pop_front();
                STDDeque.pop_front();
            }
            break;
        case 4:
            if (!XXLDeque.empty())
            {
                XXLDeque.pop_back();
                STDDeque.pop_back();
            }
            break;
        case 5:
            if (XXLDeque.size() > 0)
            {
                stxxl::deque<int>::iterator XXLI = XXLDeque.begin() + (value % XXLDeque.size());
                std::deque<int>::iterator STDI = STDDeque.begin() + (value % STDDeque.size());
                *XXLI = value;
                *STDI = value;
                unsigned value1 = rand();
                if (XXLI - XXLDeque.begin() == 0)
                    break;

                XXLI = XXLI - (value1 % (XXLI - XXLDeque.begin()));
                STDI = STDI - (value1 % (STDI - STDDeque.begin()));
                *XXLI = value1;
                *STDI = value1;
            }
            break;
        }

        die_unless(XXLDeque.empty() == STDDeque.empty());
        die_unless(XXLDeque.size() == STDDeque.size());
        die_unless(XXLDeque.end() - XXLDeque.begin() == STDDeque.end() - STDDeque.begin());
        //die_unless(std::equal(XXLDeque.begin(),XXLDeque.end(),STDDeque.begin() _STXXL_FORCE_SEQUENTIAL));
        if (XXLDeque.size() > 0)
        {
            die_unless(XXLDeque.back() == STDDeque.back());
            die_unless(XXLDeque.front() == STDDeque.front());
        }

        if (!(i % 100000))
        {
            die_unless(std::equal(XXLDeque.begin(), XXLDeque.end(), STDDeque.begin()));
            LOG1 << "Operations done: " << i << " size: " << STDDeque.size();
        }
    }

    return 0;
}

// forced instantiation
template class stxxl::deque<int>;
using deque_type = stxxl::deque<int>;
template class stxxl::deque_iterator<deque_type>;
template class stxxl::const_deque_iterator<deque_type>;

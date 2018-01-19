/***************************************************************************
 *  tests/containers/btree/test_btree_insert_erase.cpp
 *
 *  Part of the STXXL. See http://stxxl.org
 *
 *  Copyright (C) 2006 Roman Dementiev <dementiev@ira.uka.de>
 *
 *  Distributed under the Boost Software License, Version 1.0.
 *  (See accompanying file LICENSE_1_0.txt or copy at
 *  http://www.boost.org/LICENSE_1_0.txt)
 **************************************************************************/

#include <ctime>
#include <iostream>

#include <tlx/die.hpp>
#include <tlx/logger.hpp>

#include <stxxl/bits/common/comparator.h>
#include <stxxl/bits/containers/btree/btree.h>
#include <stxxl/random_shuffle>
#include <stxxl/scan>
#include <stxxl/sort>
#include <tlx/die.hpp>

using comp_type = stxxl::comparator<int>;
using btree_type = stxxl::btree::btree<
          int, double, comp_type, 4096, 4096, foxxll::simple_random>;
//using btree_type =  stxxl::btree::btree<int,double,comp_type,10,11,foxxll::simple_random> ;

std::ostream& operator << (std::ostream& o, const std::pair<int, double>& obj)
{
    o << obj.first << " " << obj.second;
    return o;
}

struct rnd_gen
{
    stxxl::random_number32 rnd;
    int operator () ()
    {
        return (rnd() >> 2) * 3;
    }
};

bool operator == (const std::pair<int, double>& a, const std::pair<int, double>& b)
{
    return a.first == b.first;
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        LOG1 << "Usage: " << argv[0] << " #log_ins";
        return -1;
    }

    const int log_nins = atoi(argv[1]);
    if (log_nins > 31) {
        LOG1 << "This test can't do more than 2^31 operations, you requested 2^" << log_nins;
        return -1;
    }

    btree_type BTree(1024 * 128, 1024 * 128);

    const size_t nins = 1ULL << log_nins;

    stxxl::ran32State = (unsigned int)time(nullptr);

    stxxl::vector<int> Values(nins);
    LOG1 << "Generating " << nins << " random values";
    stxxl::generate(Values.begin(), Values.end(), rnd_gen(), 4);

    LOG1 << "Sorting the random values";
    stxxl::sort(Values.begin(), Values.end(), comp_type(), 128 * 1024 * 1024);

    LOG1 << "Making values unique";
    stxxl::vector<int>::iterator NewEnd = std::unique(Values.begin(), Values.end());
    Values.resize(NewEnd - Values.begin());

    LOG1 << "Randomly permute input values";
    stxxl::random_shuffle(Values.begin(), Values.end(), 128 * 1024 * 1024);

    stxxl::vector<int>::const_iterator it = Values.begin();
    LOG1 << "Inserting " << Values.size() << " random values into btree";
    bool use_emplace = false;
    for ( ; it != Values.end(); ++it)
    {
        if (use_emplace)
        {
            BTree.emplace(*it, static_cast<double>(*it) + 1.0);
        }
        else
        {
            BTree.insert(std::pair<int, double>(*it, static_cast<double>(*it) + 1.0));
        }

        use_emplace = !use_emplace;
    }

    LOG1 << "Number of elements in btree: " << BTree.size();

    LOG1 << "Searching " << Values.size() << " existing elements and erasing them";
    stxxl::vector<int>::const_iterator vIt = Values.begin();

    for ( ; vIt != Values.end(); ++vIt)
    {
        btree_type::iterator bIt = BTree.find(*vIt);
        die_unless(bIt != BTree.end());
        // check at() finds it, too
        die_unless(BTree.at(*vIt) == bIt->second);
        // erasing non-existent element
        die_unless(BTree.erase((*vIt) + 1) == 0);
        // erasing existing element
        die_unless(BTree.erase(*vIt) == 1);
        // checking it is not there
        die_unless(BTree.find(*vIt) == BTree.end());
        // trying to erase it again
        die_unless(BTree.erase(*vIt) == 0);
        // checking at() throws for non-existing element
        die_unless_throws(BTree.at(*vIt), std::out_of_range);
    }

    die_unless(BTree.empty());

    LOG1 << "Test passed.";

    return 0;
}

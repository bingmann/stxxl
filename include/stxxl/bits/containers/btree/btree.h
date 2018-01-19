/***************************************************************************
 *  include/stxxl/bits/containers/btree/btree.h
 *
 *  Part of the STXXL. See http://stxxl.org
 *
 *  Copyright (C) 2006, 2008 Roman Dementiev <dementiev@ira.uka.de>
 *
 *  Distributed under the Boost Software License, Version 1.0.
 *  (See accompanying file LICENSE_1_0.txt or copy at
 *  http://www.boost.org/LICENSE_1_0.txt)
 **************************************************************************/

#ifndef STXXL_CONTAINERS_BTREE_BTREE_HEADER
#define STXXL_CONTAINERS_BTREE_BTREE_HEADER

#include <algorithm>
#include <limits>
#include <map>
#include <utility>
#include <vector>

#include <stxxl/bits/containers/btree/iterator.h>
#include <stxxl/bits/containers/btree/iterator_map.h>
#include <stxxl/bits/containers/btree/leaf.h>
#include <stxxl/bits/containers/btree/node.h>
#include <stxxl/bits/containers/btree/node_cache.h>
#include <stxxl/bits/containers/btree/root_node.h>
#include <stxxl/vector>

namespace stxxl {
namespace btree {

template <class KeyType,
          class DataType,
          class KeyCompareWithMaxType,
          unsigned RawNodeSize,
          unsigned RawLeafSize,
          class PDAllocStrategy
          >
class btree
{
    static constexpr bool debug = false;

public:
    using key_type = KeyType;
    using data_type = DataType;
    using key_compare = KeyCompareWithMaxType;

    using self_type = btree<KeyType, DataType, KeyCompareWithMaxType,
                            RawNodeSize, RawLeafSize, PDAllocStrategy>;

    using alloc_strategy_type = PDAllocStrategy;

    using size_type = external_size_type;
    using difference_type = external_diff_type;
    using value_type = std::pair<const key_type, data_type>;
    using reference = value_type &;
    using const_reference = const value_type &;
    using pointer = value_type *;
    using const_pointer = value_type const*;

    // leaf type declarations
    using leaf_type = normal_leaf<key_type, data_type, key_compare, RawLeafSize, self_type>;
    friend class normal_leaf<key_type, data_type, key_compare, RawLeafSize, self_type>;
    using leaf_block_type = typename leaf_type::block_type;
    using leaf_bid_type = typename leaf_type::bid_type;
    using leaf_cache_type = node_cache<leaf_type, self_type>;
    friend class node_cache<leaf_type, self_type>;
    // iterator types
    using iterator = btree_iterator<self_type>;
    using const_iterator = btree_const_iterator<self_type>;
    friend class btree_iterator_base<self_type>;
    // iterator map type
    using iterator_map_type = iterator_map<self_type>;
    // node type declarations
    using node_type = normal_node<key_type, key_compare, RawNodeSize, self_type>;
    using node_block_type = typename node_type::block_type;
    friend class normal_node<key_type, key_compare, RawNodeSize, self_type>;
    using node_bid_type = typename node_type::bid_type;
    using node_cache_type = node_cache<node_type, self_type>;
    friend class node_cache<node_type, self_type>;

    using value_compare = typename leaf_type::value_compare;

    enum {
        min_node_size = node_type::min_size,
        max_node_size = node_type::max_size,
        min_leaf_size = leaf_type::min_size,
        max_leaf_size = leaf_type::max_size
    };

private:
    key_compare m_key_compare;
    mutable node_cache_type m_node_cache;
    mutable leaf_cache_type m_leaf_cache;
    iterator_map_type m_iterator_map;
    size_type m_size;
    unsigned int m_height;
    bool m_prefetching_enabled;
    foxxll::block_manager* m_bm;
    alloc_strategy_type m_alloc_strategy;

    using root_node_type = std::map<key_type, node_bid_type, key_compare>;
    using root_node_iterator_type = typename root_node_type::iterator;
    using root_node_const_iterator_type = typename root_node_type::const_iterator;
    using root_node_pair_type = std::pair<key_type, node_bid_type>;

    root_node_type m_root_node;
    iterator m_end_iterator;

    void insert_into_root(const std::pair<key_type, node_bid_type>& splitter)
    {
        std::pair<root_node_iterator_type, bool> result = m_root_node.insert(splitter);
        assert(result.second);
        tlx::unused(result);

        if (m_root_node.size() > max_node_size) // root overflow
        {
            LOG << "btree::insert_into_root, overflow happened, splitting";

            node_bid_type left_bid;
            node_type* left_node = m_node_cache.get_new_node(left_bid);
            assert(left_node);
            node_bid_type right_bid;
            node_type* right_node = m_node_cache.get_new_node(right_bid);
            assert(right_node);

            const size_t old_size = m_root_node.size();
            const size_t half = m_root_node.size() / 2;
            size_t i = 0;
            root_node_iterator_type it = m_root_node.begin();
            typename node_block_type::iterator block_it = left_node->block().begin();
            while (i < half)                    // copy smaller part
            {
                *block_it = *it;
                ++i;
                ++block_it;
                ++it;
            }
            left_node->block().info.cur_size = (unsigned int)half;
            key_type left_key = (left_node->block()[half - 1]).first;

            block_it = right_node->block().begin();
            while (i < old_size)                // copy larger part
            {
                *block_it = *it;
                ++i;
                ++block_it;
                ++it;
            }

            const size_t right_size = static_cast<size_t>(old_size - half);
            right_node->block().info.cur_size = right_size;

            key_type right_key = (right_node->block()[right_size - 1]).first;

            assert(old_size == right_node->size() + left_node->size());

            // create new root node
            m_root_node.clear();
            m_root_node.insert(root_node_pair_type(left_key, left_bid));
            m_root_node.insert(root_node_pair_type(right_key, right_bid));

            ++m_height;
            LOG << "btree Increasing height to " << m_height;
            if (m_node_cache.size() < (m_height - 1))
            {
                FOXXLL_THROW2(std::runtime_error, "btree::bulk_construction",
                              "The height of the tree (" << m_height << ") has exceeded the required capacity (" << (m_node_cache.size() + 1) << ") of the node cache. Increase the node cache size.");
            }
        }
    }

    template <class CacheType>
    void fuse_or_balance(root_node_iterator_type uit, CacheType& cache)
    {
        using local_node_type = typename CacheType::node_type;
        using local_bid_type = typename local_node_type::bid_type;

        root_node_iterator_type left_it, right_it;
        if (uit->first == m_key_compare.max_value())
        {
            // uit is the last entry in the root
            assert(uit != m_root_node.begin());
            right_it = uit;
            left_it = --uit;
        }
        else
        {
            left_it = uit;
            right_it = ++uit;
            assert(right_it != m_root_node.end());
        }

        // now fuse or balance nodes pointed by leftIt and rightIt
        local_bid_type left_bid = (local_bid_type)left_it->second;
        local_bid_type right_bid = (local_bid_type)right_it->second;
        local_node_type* left_node = cache.get_node(left_bid, true);
        local_node_type* right_node = cache.get_node(right_bid, true);

        const size_t total_size = left_node->size() + right_node->size();
        if (total_size <= right_node->max_nelements())
        {
            // --- fuse ---

            // add the content of left_node to right_node
            right_node->fuse(*left_node);

            cache.unfix_node(right_bid);
            // 'delete_node' unfixes left_bid also
            cache.delete_node(left_bid);

            // delete left BID from the root
            m_root_node.erase(left_it);
        }
        else
        {
            // --- balance ---

            key_type new_splitter = right_node->balance(*left_node);

            // delete left BID from the root
            m_root_node.erase(left_it);

            // reinsert with the new key
            m_root_node.insert(root_node_pair_type(new_splitter, (node_bid_type)left_bid));

            cache.unfix_node(left_bid);
            cache.unfix_node(right_bid);
        }
    }

    void create_empty_leaf()
    {
        leaf_bid_type new_bid;
        leaf_type* new_leaf = m_leaf_cache.get_new_node(new_bid);
        assert(new_leaf);
        m_end_iterator = new_leaf->end();           // initialize end() iterator
        m_root_node.insert(
            root_node_pair_type(m_key_compare.max_value(), (node_bid_type)new_bid));
    }

    void deallocate_children()
    {
        if (m_height == 2)
        {
            // we have children leaves here
            for (root_node_const_iterator_type it = m_root_node.begin();
                 it != m_root_node.end(); ++it)
            {
                // delete from leaf cache and deallocate bid
                m_leaf_cache.delete_node((leaf_bid_type)it->second);
            }
        }
        else
        {
            for (root_node_const_iterator_type it = m_root_node.begin();
                 it != m_root_node.end(); ++it)
            {
                node_type* node = m_node_cache.get_node((node_bid_type)it->second);
                assert(node);
                node->deallocate_children(m_height - 1);
                // delete from node cache and deallocate bid
                m_node_cache.delete_node((node_bid_type)it->second);
            }
        }
    }

    template <class InputIterator>
    void bulk_construction(InputIterator begin, InputIterator end,
                           double node_fill_factor, double leaf_fill_factor)
    {
        assert(node_fill_factor >= 0.5);
        assert(leaf_fill_factor >= 0.5);
        key_type last_key = m_key_compare.max_value();

        using key_bid_pair = std::pair<key_type, node_bid_type>;
        using key_bid_vector_type = typename stxxl::vector<
                  key_bid_pair, 1, stxxl::random_pager<1>, node_block_type::raw_size
                  >;

        key_bid_vector_type bids;

        leaf_bid_type new_bid;
        leaf_type* leaf = m_leaf_cache.get_new_node(new_bid);
        const size_t max_leaf_elements = static_cast<size_t>(
            leaf->max_nelements() * leaf_fill_factor);

        while (begin != end)
        {
            // write data in leaves

            // if *b not equal to the last element
            if (m_key_compare(begin->first, last_key) || m_key_compare(last_key, begin->first))
            {
                ++m_size;
                if (leaf->size() == max_leaf_elements)
                {
                    // overflow, need a new block
                    bids.push_back(key_bid_pair(leaf->back().first, (node_bid_type)new_bid));

                    leaf_type* new_leaf = m_leaf_cache.get_new_node(new_bid);
                    assert(new_leaf);
                    // Setting links
                    leaf->succ() = new_leaf->my_bid();
                    new_leaf->pred() = leaf->my_bid();

                    leaf = new_leaf;
                }
                leaf->push_back(*begin);
                last_key = begin->first;
            }
            ++begin;
        }

        // rebalance the last leaf
        if (leaf->underflows() && !bids.empty())
        {
            leaf_type* left_leaf = m_leaf_cache.get_node((leaf_bid_type)(bids.back().second));
            assert(left_leaf);
            if (left_leaf->size() + leaf->size() <= leaf->max_nelements())
            {
                // can fuse
                leaf->fuse(*left_leaf);
                m_leaf_cache.delete_node((leaf_bid_type)(bids.back().second));
                bids.pop_back();
                assert(!leaf->overflows() && !leaf->underflows());
            }
            else
            {
                // need to rebalance
                const key_type new_splitter = leaf->balance(*left_leaf);
                bids.back().first = new_splitter;
                assert(!left_leaf->overflows() && !left_leaf->underflows());
            }
        }

        assert(!leaf->overflows() && (!leaf->underflows() || m_size <= max_leaf_size));

        m_end_iterator = leaf->end();                 // initialize end() iterator

        bids.push_back(key_bid_pair(m_key_compare.max_value(), (node_bid_type)new_bid));

        const size_t max_node_elements = static_cast<size_t>(
            max_node_size * node_fill_factor);

        //-tb fixes bug with only one child remaining in m_root_node
        while (bids.size() > node_type::max_nelements())
        {
            key_bid_vector_type parent_bids;

            size_t nparents = foxxll::div_ceil(bids.size(), max_node_elements);
            assert(nparents >= 2);
            LOG << "btree bulk construct"
                << " bids.size=" << bids.size()
                << " nparents=" << nparents
                << " max_node_elements=" << max_node_elements
                << " node_type::max_nelements=" << node_type::max_nelements();

            for (typename key_bid_vector_type::const_iterator it = bids.begin();
                 it != bids.end(); )
            {
                node_bid_type new_bid;
                node_type* node = m_node_cache.get_new_node(new_bid);
                assert(node);

                for (size_t cnt = 0;
                     cnt < max_node_elements && it != bids.end(); ++cnt, ++it)
                {
                    node->push_back(*it);
                }

                LOG << "btree bulk construct node size : " << node->size() << " limits: " << node->min_nelements() << " " << node->max_nelements() << " max_node_elements: " << max_node_elements;

                if (node->underflows())
                {
                    // this can happen only at the end
                    assert(it == bids.end());
                    assert(!parent_bids.empty());

                    node_type* left_node = m_node_cache.get_node(parent_bids.back().second);
                    assert(left_node);
                    if (left_node->size() + node->size() <= node->max_nelements())
                    {
                        // can fuse
                        LOG << "btree bulk construct fuse last nodes:"
                            << " left_node.size=" << left_node->size()
                            << " node.size=" << node->size();

                        node->fuse(*left_node);
                        m_node_cache.delete_node(parent_bids.back().second);
                        parent_bids.pop_back();
                    }
                    else
                    {
                        // need to rebalance
                        LOG << "btree bulk construct rebalance last nodes:"
                            << " left_node.size=" << left_node->size()
                            << " node.size=" << node->size();

                        const key_type new_splitter = node->balance(*left_node, false);
                        parent_bids.back().first = new_splitter;

                        LOG << "btree bulk construct after rebalance:"
                            << " left_node.size=" << left_node->size()
                            << " node.size=" << node->size();

                        assert(!left_node->overflows() && !left_node->underflows());
                    }
                }
                assert(!node->overflows() && !node->underflows());

                parent_bids.push_back(key_bid_pair(node->back().first, new_bid));
            }

            LOG << "btree parent_bids.size()=" << parent_bids.size()
                << " bids.size()=" << bids.size();

            std::swap(parent_bids, bids);

            assert(nparents == bids.size() || (nparents - 1) == bids.size());

            ++m_height;
            LOG << "Increasing height to " << m_height;
            if (m_node_cache.size() < (m_height - 1))
            {
                FOXXLL_THROW2(std::runtime_error, "btree::bulk_construction",
                              "The height of the tree (" << m_height << ") has exceeded the required capacity (" << (m_node_cache.size() + 1) << ") of the node cache. Increase the node cache size.");
            }
        }

        m_root_node.insert(bids.begin(), bids.end());

        LOG << "btree bulk root_node_.size()=" << m_root_node.size();
    }

public:
    btree(const size_t node_cache_size_in_bytes,
          const size_t leaf_cache_size_in_bytes)
        : m_node_cache(node_cache_size_in_bytes, this, m_key_compare),
          m_leaf_cache(leaf_cache_size_in_bytes, this, m_key_compare),
          m_iterator_map(this),
          m_size(0),
          m_height(2),
          m_prefetching_enabled(true),
          m_bm(foxxll::block_manager::get_instance())
    {
        LOG << "Creating a btree, addr=" << this;
        LOG << " bytes in a node: " << node_bid_type::size;
        LOG << " bytes in a leaf: " << leaf_bid_type::size;
        LOG << " elements in a node: " << node_block_type::size;
        LOG << " elements in a leaf: " << leaf_block_type::size;
        LOG << " size of a node element: " << sizeof(typename node_block_type::value_type);
        LOG << " size of a leaf element: " << sizeof(typename leaf_block_type::value_type);

        create_empty_leaf();
    }

    btree(const key_compare& key_compare,
          const size_t node_cache_size_in_bytes,
          const size_t leaf_cache_size_in_bytes)
        : m_key_compare(key_compare),
          m_node_cache(node_cache_size_in_bytes, this, m_key_compare),
          m_leaf_cache(leaf_cache_size_in_bytes, this, m_key_compare),
          m_iterator_map(this),
          m_size(0),
          m_height(2),
          m_prefetching_enabled(true),
          m_bm(foxxll::block_manager::get_instance())
    {
        LOG << "Creating a btree, addr=" << this;
        LOG << " bytes in a node: " << node_bid_type::size;
        LOG << " bytes in a leaf: " << leaf_bid_type::size;

        create_empty_leaf();
    }

    //! non-copyable: delete copy-constructor
    btree(const btree&) = delete;
    //! non-copyable: delete assignment operator
    btree& operator = (const btree&) = delete;

    virtual ~btree()
    {
        try
        {
            deallocate_children();
        }
        catch (...)
        {
            // no exceptions in destructor
        }
    }

    size_type size() const
    {
        return m_size;
    }

    size_type max_size() const
    {
        return std::numeric_limits<size_type>::max();
    }

    bool empty() const
    {
        return !m_size;
    }

    std::pair<iterator, bool> insert(const value_type& x)
    {
        return insert_impl(x);
    }

    std::pair<iterator, bool> insert(value_type&& x)
    {
        return insert_impl(std::forward<value_type>(x));
    }

    void insert(std::initializer_list<value_type> ilist)
    {
        for (auto&& val : ilist)
        {
            insert_impl(std::forward<decltype(val)>(val));
        }
    }

    template <class... Args>
    std::pair<iterator, bool> emplace(Args&&... args)
    {
        return insert_impl(value_type(std::forward<Args>(args)...));
    }

private:
    template <typename V>
    std::pair<iterator, bool> insert_impl(V&& x)
    {
        root_node_iterator_type it = m_root_node.lower_bound(x.first);
        assert(!m_root_node.empty());
        assert(it != m_root_node.end());

        if (m_height == 2)                // 'it' points to a leaf
        {
            LOG << "Inserting new value into a leaf";
            leaf_type* leaf = m_leaf_cache.get_node((leaf_bid_type)it->second, true);
            assert(leaf);
            std::pair<key_type, leaf_bid_type> splitter;
            std::pair<iterator, bool> result = leaf->insert(std::forward<V>(x), splitter);
            if (result.second)
                ++m_size;

            m_leaf_cache.unfix_node((leaf_bid_type)it->second);
            //if(key_compare::max_value() == Splitter.first)
            if (!(m_key_compare(m_key_compare.max_value(), splitter.first) ||
                  m_key_compare(splitter.first, m_key_compare.max_value())))
                return result;
            // no overflow/splitting happened

            LOG << "Inserting new value into root node";

            insert_into_root(std::make_pair(splitter.first, node_bid_type(splitter.second)));

            assert(m_leaf_cache.nfixed() == 0);
            assert(m_node_cache.nfixed() == 0);
            return result;
        }

        // 'it' points to a node
        LOG << "Inserting new value into a node";
        node_type* node = m_node_cache.get_node((node_bid_type)it->second, true);
        assert(node);
        std::pair<key_type, node_bid_type> splitter;
        std::pair<iterator, bool> result = node->insert(std::forward<V>(x), m_height - 1, splitter);
        if (result.second)
            ++m_size;

        m_node_cache.unfix_node((node_bid_type)it->second);
        //if(key_compare::max_value() == Splitter.first)
        if (!(m_key_compare(m_key_compare.max_value(), splitter.first) ||
              m_key_compare(splitter.first, m_key_compare.max_value())))
            return result;
        // no overflow/splitting happened

        LOG << "Inserting new value into root node";

        insert_into_root(splitter);

        assert(m_leaf_cache.nfixed() == 0);
        assert(m_node_cache.nfixed() == 0);

        return result;
    }

public:
    iterator begin()
    {
        root_node_iterator_type it = m_root_node.begin();
        assert(it != m_root_node.end());

        if (m_height == 2)                // 'it' points to a leaf
        {
            LOG << "btree: retrieving begin() from the first leaf";
            leaf_type* leaf = m_leaf_cache.get_node((leaf_bid_type)it->second);
            assert(leaf);

            assert(m_leaf_cache.nfixed() == 0);
            assert(m_node_cache.nfixed() == 0);
            return leaf->begin();
        }

        // 'it' points to a node
        LOG << "btree: retrieving begin() from the first node";
        node_type* node = m_node_cache.get_node((node_bid_type)it->second, true);
        assert(node);
        iterator result = node->begin(m_height - 1);
        m_node_cache.unfix_node((node_bid_type)it->second);

        assert(m_leaf_cache.nfixed() == 0);
        assert(m_node_cache.nfixed() == 0);

        return result;
    }

    const_iterator begin() const
    {
        root_node_const_iterator_type it = m_root_node.begin();
        assert(it != m_root_node.end());

        if (m_height == 2)                // 'it' points to a leaf
        {
            LOG << "btree: retrieving begin() from the first leaf";
            const leaf_type* leaf = m_leaf_cache.get_const_node((leaf_bid_type)it->second);
            assert(leaf);
            assert(m_leaf_cache.nfixed() == 0);
            assert(m_node_cache.nfixed() == 0);
            return leaf->begin();
        }

        // 'it' points to a node
        LOG << "btree: retrieving begin() from the first node";
        const node_type* node = m_node_cache.get_const_node((node_bid_type)it->second, true);
        assert(node);
        const_iterator result = node->begin(m_height - 1);
        m_node_cache.unfix_node((node_bid_type)it->second);
        assert(m_leaf_cache.nfixed() == 0);
        assert(m_node_cache.nfixed() == 0);
        return result;
    }

    iterator end()
    {
        return m_end_iterator;
    }

    const_iterator end() const
    {
        return m_end_iterator;
    }

    data_type& operator [] (const key_type& k)
    {
        return (*((insert(value_type(k, data_type()))).first)).second;
    }

    //! Returns a reference to the mapped value of the element with
    //! key equivalent to key. If no such element exists, an exception
    //! of type std::out_of_range is thrown.
    data_type& at(const key_type& k)
    {
        iterator it = find(k);

        if (UNLIKELY(it == end()))
        {
            throw std::out_of_range("stxxl::btree");
        }

        return it->second;
    }

    //! Returns a reference to the mapped value of the element with
    //! key equivalent to key. If no such element exists, an exception
    //! of type std::out_of_range is thrown.
    const data_type& at(const key_type& k) const
    {
        const_iterator it = find(k);

        if (UNLIKELY(it == end()))
        {
            throw std::out_of_range("stxxl::btree");
        }

        return it->second;
    }

    iterator find(const key_type& k)
    {
        root_node_iterator_type it = m_root_node.lower_bound(k);
        assert(it != m_root_node.end());

        if (m_height == 2)                // 'it' points to a leaf
        {
            LOG << "Searching in a leaf";
            leaf_type* leaf = m_leaf_cache.get_node((leaf_bid_type)it->second, true);
            assert(leaf);
            iterator result = leaf->find(k);
            m_leaf_cache.unfix_node((leaf_bid_type)it->second);
            assert(result == end() || result->first == k);
            assert(m_leaf_cache.nfixed() == 0);
            assert(m_node_cache.nfixed() == 0);
            return result;
        }

        // 'it' points to a node
        LOG << "Searching in a node";
        node_type* node = m_node_cache.get_node((node_bid_type)it->second, true);
        assert(node);
        iterator result = node->find(k, m_height - 1);
        m_node_cache.unfix_node((node_bid_type)it->second);

        assert(result == end() || result->first == k);
        assert(m_leaf_cache.nfixed() == 0);
        assert(m_node_cache.nfixed() == 0);
        return result;
    }

    const_iterator find(const key_type& k) const
    {
        root_node_const_iterator_type it = m_root_node.lower_bound(k);
        assert(it != m_root_node.end());

        if (m_height == 2)                // 'it' points to a leaf
        {
            LOG << "Searching in a leaf";
            const leaf_type* leaf = m_leaf_cache.get_const_node((leaf_bid_type)it->second, true);
            assert(leaf);
            const_iterator result = leaf->find(k);
            m_leaf_cache.unfix_node((leaf_bid_type)it->second);
            assert(result == end() || result->first == k);
            assert(m_leaf_cache.nfixed() == 0);
            assert(m_node_cache.nfixed() == 0);
            return result;
        }

        // 'it' points to a node
        LOG << "Searching in a node";
        const node_type* node = m_node_cache.get_const_node((node_bid_type)it->second, true);
        assert(node);
        const_iterator result = node->find(k, m_height - 1);
        m_node_cache.unfix_node((node_bid_type)it->second);

        assert(result == end() || result->first == k);
        assert(m_leaf_cache.nfixed() == 0);
        assert(m_node_cache.nfixed() == 0);
        return result;
    }

    iterator lower_bound(const key_type& k)
    {
        root_node_iterator_type it = m_root_node.lower_bound(k);
        assert(it != m_root_node.end());

        if (m_height == 2)                // 'it' points to a leaf
        {
            LOG << "Searching lower bound in a leaf";
            leaf_type* leaf = m_leaf_cache.get_node((leaf_bid_type)it->second, true);
            assert(leaf);
            iterator result = leaf->lower_bound(k);
            m_leaf_cache.unfix_node((leaf_bid_type)it->second);
            assert(m_leaf_cache.nfixed() == 0);
            assert(m_node_cache.nfixed() == 0);
            return result;
        }

        // 'it' points to a node
        LOG << "Searching lower bound in a node";
        node_type* node = m_node_cache.get_node((node_bid_type)it->second, true);
        assert(node);
        iterator result = node->lower_bound(k, m_height - 1);
        m_node_cache.unfix_node((node_bid_type)it->second);

        assert(m_leaf_cache.nfixed() == 0);
        assert(m_node_cache.nfixed() == 0);
        return result;
    }

    const_iterator lower_bound(const key_type& k) const
    {
        root_node_const_iterator_type it = m_root_node.lower_bound(k);
        assert(it != m_root_node.end());

        if (m_height == 2)                // 'it' points to a leaf
        {
            LOG << "Searching lower bound in a leaf";
            const leaf_type* leaf = m_leaf_cache.get_const_node((leaf_bid_type)it->second, true);
            assert(leaf);
            const_iterator result = leaf->lower_bound(k);
            m_leaf_cache.unfix_node((leaf_bid_type)it->second);

            assert(m_leaf_cache.nfixed() == 0);
            assert(m_node_cache.nfixed() == 0);
            return result;
        }

        // 'it' points to a node
        LOG << "Searching lower bound in a node";
        const node_type* node = m_node_cache.get_const_node((node_bid_type)it->second, true);
        assert(node);
        const_iterator result = node->lower_bound(k, m_height - 1);
        m_node_cache.unfix_node((node_bid_type)it->second);

        assert(m_leaf_cache.nfixed() == 0);
        assert(m_node_cache.nfixed() == 0);
        return result;
    }

    iterator upper_bound(const key_type& k)
    {
        root_node_iterator_type it = m_root_node.upper_bound(k);
        assert(it != m_root_node.end());

        if (m_height == 2)                // 'it' points to a leaf
        {
            LOG << "Searching upper bound in a leaf";
            leaf_type* Leaf = m_leaf_cache.get_node((leaf_bid_type)it->second, true);
            assert(Leaf);
            iterator result = Leaf->upper_bound(k);
            m_leaf_cache.unfix_node((leaf_bid_type)it->second);

            assert(m_leaf_cache.nfixed() == 0);
            assert(m_node_cache.nfixed() == 0);
            return result;
        }

        // 'it' points to a node
        LOG << "Searching upper bound in a node";
        node_type* Node = m_node_cache.get_node((node_bid_type)it->second, true);
        assert(Node);
        iterator result = Node->upper_bound(k, m_height - 1);
        m_node_cache.unfix_node((node_bid_type)it->second);

        assert(m_leaf_cache.nfixed() == 0);
        assert(m_node_cache.nfixed() == 0);
        return result;
    }

    const_iterator upper_bound(const key_type& k) const
    {
        root_node_const_iterator_type it = m_root_node.upper_bound(k);
        assert(it != m_root_node.end());

        if (m_height == 2)                // 'it' points to a leaf
        {
            LOG << "Searching upper bound in a leaf";
            const leaf_type* leaf = m_leaf_cache.get_const_node((leaf_bid_type)it->second, true);
            assert(leaf);
            const_iterator result = leaf->upper_bound(k);
            m_leaf_cache.unfix_node((leaf_bid_type)it->second);

            assert(m_leaf_cache.nfixed() == 0);
            assert(m_node_cache.nfixed() == 0);
            return result;
        }

        // 'it' points to a node
        LOG << "Searching upper bound in a node";
        const node_type* node = m_node_cache.get_const_node((node_bid_type)it->second, true);
        assert(node);
        const_iterator result = node->upper_bound(k, m_height - 1);
        m_node_cache.unfix_node((node_bid_type)it->second);

        assert(m_leaf_cache.nfixed() == 0);
        assert(m_node_cache.nfixed() == 0);
        return result;
    }

    std::pair<iterator, iterator> equal_range(const key_type& k)
    {
        // l->first >= k
        iterator l = lower_bound(k);

        // if (k < l->first)
        if (l == end() || m_key_compare(k, l->first))
            // then upper_bound == lower_bound
            return std::pair<iterator, iterator>(l, l);

        iterator u = l;
        // only one element ==k can exist
        ++u;

        assert(m_leaf_cache.nfixed() == 0);
        assert(m_node_cache.nfixed() == 0);

        // then upper_bound == (lower_bound+1)
        return std::pair<iterator, iterator>(l, u);
    }

    std::pair<const_iterator, const_iterator> equal_range(const key_type& k) const
    {
        // l->first >= k
        const_iterator l = lower_bound(k);

        // if (k < l->first)
        if (l == end() || m_key_compare(k, l->first))
            // then upper_bound == lower_bound
            return std::pair<const_iterator, const_iterator>(l, l);

        const_iterator u = l;
        // only one element ==k can exist
        ++u;

        assert(m_leaf_cache.nfixed() == 0);
        assert(m_node_cache.nfixed() == 0);
        // then upper_bound == (lower_bound+1)
        return std::pair<const_iterator, const_iterator>(l, u);
    }

    size_type erase(const key_type& k)
    {
        root_node_iterator_type it = m_root_node.lower_bound(k);
        assert(it != m_root_node.end());

        if (m_height == 2)                // 'it' points to a leaf
        {
            LOG << "Deleting key from a leaf";
            leaf_type* Leaf = m_leaf_cache.get_node((leaf_bid_type)it->second, true);
            assert(Leaf);
            size_type result = Leaf->erase(k);
            m_size -= result;
            m_leaf_cache.unfix_node((leaf_bid_type)it->second);
            assert(m_leaf_cache.nfixed() == 0);
            assert(m_node_cache.nfixed() == 0);

            if ((!Leaf->underflows()) || m_root_node.size() == 1)
                return result;
            // no underflow or root has a special degree 1 (too few elements)

            LOG << "btree: Fusing or rebalancing a leaf";
            fuse_or_balance(it, m_leaf_cache);

            assert(m_leaf_cache.nfixed() == 0);
            assert(m_node_cache.nfixed() == 0);

            return result;
        }

        // 'it' points to a node
        LOG << "Deleting key from a node";
        assert(m_root_node.size() >= 2);
        node_type* node = m_node_cache.get_node((node_bid_type)it->second, true);
        assert(node);
        size_type result = node->erase(k, m_height - 1);
        m_size -= result;
        m_node_cache.unfix_node((node_bid_type)it->second);
        assert(m_leaf_cache.nfixed() == 0);
        assert(m_node_cache.nfixed() == 0);
        if (!node->underflows())
            return result;
        // no underflow happened

        LOG << "Fusing or rebalancing a node";
        fuse_or_balance(it, m_node_cache);

        if (m_root_node.size() == 1)
        {
            LOG << "btree Root has size 1 and height > 2";
            LOG << "btree Deallocate root and decrease height";
            it = m_root_node.begin();
            node_bid_type root_bid = it->second;
            assert(it->first == m_key_compare.max_value());
            node_type* root_node = m_node_cache.get_node(root_bid);
            assert(root_node);
            assert(root_node->back().first == m_key_compare.max_value());
            m_root_node.clear();
            m_root_node.insert(root_node->block().begin(),
                               root_node->block().begin() + root_node->size());

            m_node_cache.delete_node(root_bid);
            --m_height;
            LOG << "btree Decreasing height to " << m_height;
        }

        assert(m_leaf_cache.nfixed() == 0);
        assert(m_node_cache.nfixed() == 0);

        return result;
    }

    size_type count(const key_type& k)
    {
        if (find(k) == end())
            return 0;

        return 1;
    }

    void erase(iterator pos)
    {
        assert(pos != end());
#ifndef NDEBUG
        size_type old_size = size();
#endif

        erase(pos->first);

        assert(size() == old_size - 1);
    }

    iterator insert(const_iterator /*pos*/, const value_type& x)
    {
        // pos ignored in the current version
        return insert(x).first;
    }

    template <class... Args>
    iterator emplace_hint(iterator hint, Args&&... args)
    {
        return insert(hint, value_type(std::forward<Args>(args)...));
    }

    void clear()
    {
        deallocate_children();

        m_root_node.clear();

        m_size = 0;
        m_height = 2,

        create_empty_leaf();
        assert(m_leaf_cache.nfixed() == 0);
        assert(m_node_cache.nfixed() == 0);
    }

    template <class InputIterator>
    void insert(InputIterator b, InputIterator e)
    {
        while (b != e)
        {
            insert(*(b++));
        }
    }

    template <class InputIterator>
    btree(InputIterator begin,
          InputIterator end,
          const key_compare& c_,
          const size_t node_cache_size_in_bytes,
          const size_t leaf_cache_size_in_bytes,
          bool range_sorted = false,
          double node_fill_factor = 0.75,
          double leaf_fill_factor = 0.6)
        : m_key_compare(c_),
          m_node_cache(node_cache_size_in_bytes, this, m_key_compare),
          m_leaf_cache(leaf_cache_size_in_bytes, this, m_key_compare),
          m_iterator_map(this),
          m_size(0),
          m_height(2),
          m_prefetching_enabled(true),
          m_bm(foxxll::block_manager::get_instance())
    {
        LOG << "Creating a btree, addr=" << this;
        LOG << " bytes in a node: " << node_bid_type::size;
        LOG << " bytes in a leaf: " << leaf_bid_type::size;

        if (range_sorted == false)
        {
            create_empty_leaf();
            insert(begin, end);
            assert(m_leaf_cache.nfixed() == 0);
            assert(m_node_cache.nfixed() == 0);
            return;
        }

        bulk_construction(begin, end, node_fill_factor, leaf_fill_factor);
        assert(m_leaf_cache.nfixed() == 0);
        assert(m_node_cache.nfixed() == 0);
    }

    template <class InputIterator>
    btree(InputIterator begin,
          InputIterator end,
          const size_t node_cache_size_in_bytes,
          const size_t leaf_cache_size_in_bytes,
          bool range_sorted = false,
          double node_fill_factor = 0.75,
          double leaf_fill_factor = 0.6)
        : m_node_cache(node_cache_size_in_bytes, this, m_key_compare),
          m_leaf_cache(leaf_cache_size_in_bytes, this, m_key_compare),
          m_iterator_map(this),
          m_size(0),
          m_height(2),
          m_prefetching_enabled(true),
          m_bm(foxxll::block_manager::get_instance())
    {
        LOG << "Creating a btree, addr=" << this;
        LOG << " bytes in a node: " << node_bid_type::size;
        LOG << " bytes in a leaf: " << leaf_bid_type::size;

        if (range_sorted == false)
        {
            create_empty_leaf();
            insert(begin, end);
            assert(m_leaf_cache.nfixed() == 0);
            assert(m_node_cache.nfixed() == 0);
            return;
        }

        bulk_construction(begin, end, node_fill_factor, leaf_fill_factor);
        assert(m_leaf_cache.nfixed() == 0);
        assert(m_node_cache.nfixed() == 0);
    }

    void erase(iterator first, iterator last)
    {
        if (first == begin() && last == end())
            clear();
        else
            while (first != last)
                erase(first++);
    }

    key_compare key_comp() const
    {
        return m_key_compare;
    }
    value_compare value_comp() const
    {
        return value_compare(m_key_compare);
    }

    void swap(btree& obj)
    {
        std::swap(m_key_compare, obj.m_key_compare);   // OK

        std::swap(m_node_cache, obj.m_node_cache);     // OK
        std::swap(m_leaf_cache, obj.m_leaf_cache);     // OK

        std::swap(m_iterator_map, obj.m_iterator_map); // must update all iterators

        std::swap(m_end_iterator, obj.m_end_iterator);
        std::swap(m_size, obj.m_size);
        std::swap(m_height, obj.m_height);
        std::swap(m_alloc_strategy, obj.m_alloc_strategy);
        std::swap(m_root_node, obj.m_root_node);
    }

    void enable_prefetching()
    {
        m_prefetching_enabled = true;
    }
    void disable_prefetching()
    {
        m_prefetching_enabled = false;
    }
    bool prefetching_enabled()
    {
        return m_prefetching_enabled;
    }

    void print_statistics(std::ostream& o) const
    {
        o << "Node cache statistics:" << std::endl;
        m_node_cache.print_statistics(o);
        o << "Leaf cache statistics:" << std::endl;
        m_leaf_cache.print_statistics(o);
    }
    void reset_statistics()
    {
        m_node_cache.reset_statistics();
        m_leaf_cache.reset_statistics();
    }
};

template <class KeyType,
          class DataType,
          class KeyCompareWithMaxType,
          unsigned LogNodeSize,
          unsigned LogLeafSize,
          class PDAllocStrategy
          >
inline bool operator ==
    (const btree<KeyType, DataType, KeyCompareWithMaxType,
                 LogNodeSize, LogLeafSize, PDAllocStrategy>& a,
    const btree<KeyType, DataType, KeyCompareWithMaxType,
                LogNodeSize, LogLeafSize, PDAllocStrategy>& b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
}

template <class KeyType,
          class DataType,
          class KeyCompareWithMaxType,
          unsigned LogNodeSize,
          unsigned LogLeafSize,
          class PDAllocStrategy
          >
inline bool operator !=
    (const btree<KeyType, DataType, KeyCompareWithMaxType,
                 LogNodeSize, LogLeafSize, PDAllocStrategy>& a,
    const btree<KeyType, DataType, KeyCompareWithMaxType,
                LogNodeSize, LogLeafSize, PDAllocStrategy>& b)
{
    return !(a == b);
}

template <class KeyType,
          class DataType,
          class KeyCompareWithMaxType,
          unsigned LogNodeSize,
          unsigned LogLeafSize,
          class PDAllocStrategy
          >
inline bool operator <
    (const btree<KeyType, DataType, KeyCompareWithMaxType,
                 LogNodeSize, LogLeafSize, PDAllocStrategy>& a,
    const btree<KeyType, DataType, KeyCompareWithMaxType,
                LogNodeSize, LogLeafSize, PDAllocStrategy>& b)
{
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
}

template <class KeyType,
          class DataType,
          class KeyCompareWithMaxType,
          unsigned LogNodeSize,
          unsigned LogLeafSize,
          class PDAllocStrategy
          >
inline bool operator >
    (const btree<KeyType, DataType, KeyCompareWithMaxType,
                 LogNodeSize, LogLeafSize, PDAllocStrategy>& a,
    const btree<KeyType, DataType, KeyCompareWithMaxType,
                LogNodeSize, LogLeafSize, PDAllocStrategy>& b)
{
    return b < a;
}

template <class KeyType,
          class DataType,
          class KeyCompareWithMaxType,
          unsigned LogNodeSize,
          unsigned LogLeafSize,
          class PDAllocStrategy
          >
inline bool operator <=
    (const btree<KeyType, DataType, KeyCompareWithMaxType,
                 LogNodeSize, LogLeafSize, PDAllocStrategy>& a,
    const btree<KeyType, DataType, KeyCompareWithMaxType,
                LogNodeSize, LogLeafSize, PDAllocStrategy>& b)
{
    return !(b < a);
}

template <class KeyType,
          class DataType,
          class KeyCompareWithMaxType,
          unsigned LogNodeSize,
          unsigned LogLeafSize,
          class PDAllocStrategy
          >
inline bool operator >=
    (const btree<KeyType, DataType, KeyCompareWithMaxType,
                 LogNodeSize, LogLeafSize, PDAllocStrategy>& a,
    const btree<KeyType, DataType, KeyCompareWithMaxType,
                LogNodeSize, LogLeafSize, PDAllocStrategy>& b)
{
    return !(a < b);
}

} // namespace btree
} // namespace stxxl

namespace std {

template <class KeyType,
          class DataType,
          class KeyCompareWithMaxType,
          unsigned LogNodeSize,
          unsigned LogLeafSize,
          class PDAllocStrategy
          >
void swap(stxxl::btree::btree<KeyType, DataType, KeyCompareWithMaxType,
                              LogNodeSize, LogLeafSize, PDAllocStrategy>& a,
          stxxl::btree::btree<KeyType, DataType, KeyCompareWithMaxType,
                              LogNodeSize, LogLeafSize, PDAllocStrategy>& b)
{
    if (&a != &b)
        a.swap(b);
}

} // namespace std

#endif // !STXXL_CONTAINERS_BTREE_BTREE_HEADER

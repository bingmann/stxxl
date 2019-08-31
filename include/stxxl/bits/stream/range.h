/***************************************************************************
 *  include/stxxl/bits/stream/range.h
 *
 *  Part of the STXXL. See http://stxxl.org
 *
 *  Copyright (C) 2018 Michael Hamann <michael.hamann@kit.edu>
 *
 *  Distributed under the Boost Software License, Version 1.0.
 *  (See accompanying file LICENSE_1_0.txt or copy at
 *  http://www.boost.org/LICENSE_1_0.txt)
 **************************************************************************/

#ifndef STXXL_STREAM_RANGE_HEADER
#define STXXL_STREAM_RANGE_HEADER

#include <tlx/define/likely.hpp>

#include <functional>

namespace stxxl {

//! Stream package subnamespace.
namespace stream {

//! \addtogroup streampack
//! \{

//! An output range adapter for a stream, to be used in range-based for loops.
template <typename InputStream>
class stream2range
{
private:
    InputStream& m_input;

public:
    //! Type of the elements of the stream and range
    using value_type = typename InputStream::value_type;

    class range {
    private:
        InputStream* m_stream;

    public:
        //! Construct a range for the given stream.
        range(InputStream* stream) : m_stream(stream)
        { }

        //! Copy constructor: delete as the semantics are broken,
        //! copies would not be independent.
        range(const range&) = delete;

        //! Copy assignment: delete as the semantics are broken,
        //! copies would not be independent.
        range& operator=(const range&) = delete;

        //! Move constructor
        range(range&&) = default;

        //! Move assignment
        range& operator=(range&&) = default;

        //! Increment underlying stream.
        range& operator ++ ()
        {
            ++(*m_stream);

            if (TLX_UNLIKELY(m_stream->empty())) {
                m_stream = nullptr;
            }

            return *this;
        }

        //! Check equality with another range
        //!
        //! All ranges pointing to the same stream are equal.
        //! Ranges pointing past the end are all equal.
        bool operator == (const range& other)
        {
            return m_stream == other.m_stream;
        }

        //! Check inequality with another range
        //!
        //! All ranges pointing to the same stream are equal.
        //! Ranges pointing past the end are all equal.
        bool operator != (const range& other)
        {
            return m_stream != other.m_stream;
        }

        //! Dereference the underlying stream.
        //!
        //! Dereferencing a range that is past the end is a null
        //! pointer dereference.
        const value_type& operator * ()
        {
            return *(*m_stream);
        }

        const value_type* operator -> () const
        {
            return &(*(*m_stream));
        }
    };

    //! Initialize the stream2range container
    //!
    //! Stores a reference to the given input.
    stream2range(InputStream& input)
        : m_input(input)
    { }

    //! Return a range pointing to the current position of the underlying stream.
    range begin() const
    {
        return range(&this->m_input);
    }

    //! Return a range pointing past the end of any stream.
    range end() const
    {
        return range(nullptr);
    }
};

//! Utility function to construct a stream2range container from the given stream.
template <typename InputStream>
stream2range<InputStream> range(InputStream& input)
{
    return stream2range<InputStream>(input);
}


//! \}

} // namespace stream
} // namespace stxxl

#endif // !STXXL_STREAM_RANGE_HEADER

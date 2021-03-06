/**
 * Copyright (c) 2015 Eugene Lazin <4lazin@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "stringpool.h"
#include <boost/regex.hpp>

namespace Akumuli {

//                       //
//      String Pool      //
//                       //

StringPool::StringPool()
    : counter{0}
{
}

StringPool::StringT StringPool::add(const char* begin, const char* end) {
    std::lock_guard<std::mutex> guard(pool_mutex);  // Maybe I'll need to optimize this
    if (pool.empty()) {
        pool.emplace_back();
        pool.back().reserve(static_cast<size_t>(MAX_BIN_SIZE));
    }
    int size = static_cast<int>(end - begin);
    if (size == 0) {
        return std::make_pair("", 0);
    }
    size += 1;  // 1 is for 0 character
    std::vector<char>* bin = &pool.back();
    if (static_cast<int>(bin->size()) + size > MAX_BIN_SIZE) {
        // New bin
        pool.emplace_back();
        bin = &pool.back();
        bin->reserve(static_cast<size_t>(MAX_BIN_SIZE));
    }
    for(auto i = begin; i < end; i++) {
        bin->push_back(*i);
    }
    bin->push_back('\0');
    const char* p = &bin->back();
    p -= size - 1;
    int token_size = static_cast<int>(end - begin);
    std::atomic_fetch_add(&counter, 1ul);
    return std::make_pair(p, token_size);
}

size_t StringPool::size() const {
    return std::atomic_load(&counter);
}

std::vector<StringPool::StringT> StringPool::regex_match(const char *regex, StringPoolOffset *offset, size_t* psize) const {
    std::vector<StringPool::StringT> results;
    boost::regex series_regex(regex, boost::regex_constants::optimize);
    typedef std::vector<char> const* PBuffer;
    std::vector<PBuffer> buffers;
    {
        std::lock_guard<std::mutex> guard(pool_mutex);
        if (psize) {
            *psize = size();
        }
        for(auto& buf: pool) {
            buffers.push_back(&buf);
        }
    }
    size_t buffers_skip = 0;
    if (offset != nullptr && offset->buffer_offset != 0) {
        buffers_skip = offset->buffer_offset;
    }
    size_t first_row_skip = 0;
    if (offset != nullptr && offset->offset != 0) {
        first_row_skip = offset->offset;
    }
    for(auto pbuf: buffers) {
        if (buffers_skip == 0) {
            // buffer space to search
            auto bufbegin = pbuf->data() + first_row_skip;
            auto bufend = pbuf->data() + pbuf->size();
            // should be used to skip data only in a first row
            first_row_skip = 0;
            // regex search
            auto begin = boost::cregex_iterator(bufbegin, bufend, series_regex);
            auto end = boost::cregex_iterator();
            for(boost::cregex_iterator i = begin; i != end; i++) {
                boost::cmatch match = *i;
                if (match[0].matched) {
                    const char* b = match[0].first;
                    const char* e = match[0].second;
                    size_t sz = e - b;
                    // This value can be a false positive, e.g the series name can look like this:
                    //  "cpu.sys host=host_123 OS=Ubuntu_14.04"
                    // and regex can search for `host=host_123` pattern followed by arbitrary number of
                    // other tags. Ill formed regex can match first part of the name like this:
                    // "cpu.sys host=host_1234 OS=Ubuntu_14.04" -> "cpu.sys host=host_123".
                    // That's why we need to check that every match is followed by the \0 character.
                    if (*e == 0) {
                        results.push_back(std::make_pair(b, sz));
                    }
                }
            }
        } else {
            buffers_skip--;
        }
    }
    if (offset != nullptr) {
        if (buffers.empty()) {
            offset->buffer_offset = 0;
            offset->offset = 0;
        } else {
            offset->buffer_offset = buffers.size() - 1;
            offset->offset = buffers.back()->size();
        }
    }
    return results;
}

size_t StringTools::hash(StringT str) {
    // implementation of Dan Bernstein's djb2
    const char* begin = str.first;
    int len = str.second;
    const char* end = begin + len;
    size_t hash = 5381;
    size_t c;
    while (begin < end) {
        c = static_cast<size_t>(*begin++);
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }
    return hash;
}

bool StringTools::equal(StringT lhs, StringT rhs) {
    if (lhs.second != rhs.second) {
        return false;
    }
    return std::equal(lhs.first, lhs.first + lhs.second, rhs.first);
}

StringTools::TableT StringTools::create_table(size_t size) {
    return TableT(size, &StringTools::hash, &StringTools::equal);
}

StringTools::SetT StringTools::create_set(size_t size) {
    return SetT(size, &StringTools::hash, &StringTools::equal);
}

}


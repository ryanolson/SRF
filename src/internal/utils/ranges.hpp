/**
 * SPDX-FileCopyrightText: Copyright (c) 2020-2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "srf/node/type_traits.hpp"

#include <algorithm>
#include <numeric>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace srf {

template <typename T>
std::vector<std::pair<T, T>> find_ranges(const std::vector<T>& values)
{
    static_assert(std::is_integral<T>::value, "only integral types allowed");

    auto copy = values;
    std::sort(copy.begin(), copy.end());

    std::vector<std::pair<T, T>> ranges;

    auto it  = copy.cbegin();
    auto end = copy.cend();

    while (it != end)
    {
        auto low  = *it;
        auto high = *it;
        for (T i = 0; it != end && low + i == *it; it++, i++)
        {
            high = *it;
        }
        ranges.push_back(std::make_pair(low, high));
    }

    return ranges;
}

template <typename T>
std::string print_ranges(const std::vector<std::pair<T, T>>& ranges)
{
    return std::accumulate(std::begin(ranges), std::end(ranges), std::string(), [](std::string r, std::pair<T, T> p) {
        if (p.first == p.second)
        {
            return r + (r.empty() ? "" : ",") + std::to_string(p.first);
        }

        return r + (r.empty() ? "" : ",") + std::to_string(p.first) + "-" + std::to_string(p.second);
    });
}

template <typename AssociativeT>
auto ranges_associative_to_vector(const AssociativeT& assoc)
{
    using value_t = typename AssociativeT::value_type;

    return std::vector<value_t>{assoc.begin(), assoc.end()};
}

template <typename SeqT, typename FuncT>
auto ranges_filter(const SeqT& seq, FuncT func)
{
    SeqT result{};

    std::copy_if(seq.cbegin(), seq.cend(), std::back_inserter(result), func);

    return result;
}

template <typename SeqT, typename FuncT>
auto ranges_map(const SeqT& seq, FuncT func)
{
    using const_ref_t = typename SeqT::const_reference;
    using return_t    = decltype(func(std::declval<const_ref_t>()));

    typename node::rebind_container<SeqT, return_t>::type result{};

    std::transform(seq.cbegin(), seq.cend(), std::back_inserter(result), func);

    return result;
}

}  // namespace srf

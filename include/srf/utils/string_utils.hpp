/**
 * SPDX-FileCopyrightText: Copyright (c) 2021-2022,NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "srf/utils/macros.hpp"

#include <sstream>
#include <string>
#include <tuple>
#include <utility>

namespace srf::utils {

// WARNING!!! Dont include this file in header files to avoid long compilation times with the operator<<
struct StringUtil
{
    /**
     * TODO(Documentation)
     */
    template <typename IterT>
    static std::string join(IterT begin, IterT end, std::string const& separator)
    {
        std::ostringstream result;
        if (begin != end)
        {
            result << *begin++;
        }
        while (begin != end)
        {
            result << separator << *begin++;
        }
        return result.str();
    }

    /**
     * TODO(Documentation)
     */
    template <typename IterT>
    static std::string array_to_str(IterT begin, IterT end)
    {
        return SRF_CONCAT_STR("[" << join(begin, end, ", ") << "]");
    }

    /**
     * TODO(Documentation)
     */
    static bool str_contains(const std::string& str, const std::string& search_str);
};

// Pretty print pair
template <class ChT, class TrT, typename LeftT, typename RightT>
auto& operator<<(std::basic_ostream<ChT, TrT>& os, std::pair<LeftT, RightT> const& t)
{
    os << "(" << t.first << ", " << t.second << ")";
    return os;
}

// Pretty print tuple
template <class ChT, class TrT, class... ArgsT>
auto& operator<<(std::basic_ostream<ChT, TrT>& os, std::tuple<ArgsT...> const& t)
{
    os << "(";
    std::apply([&os](auto&&... args) { ((os << args << ", "), ...); }, t);
    os << ")";
    return os;
}

}  // namespace srf::utils

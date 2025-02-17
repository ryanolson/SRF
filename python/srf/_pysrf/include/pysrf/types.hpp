/**
 * SPDX-FileCopyrightText: Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include <pysrf/utils.hpp>

#include <srf/segment/object.hpp>

#include <rxcpp/rx.hpp>  // IWYU pragma: keep

#include <rxcpp/rx-observable.hpp>
#include <rxcpp/rx-observer.hpp>
#include <rxcpp/rx-subscriber.hpp>
#include <rxcpp/rx-subscription.hpp>

#include <functional>

namespace srf::pysrf {

using PyHolder = PyObjectHolder;  // NOLINT

// using PySubscription     = srf::Subscription;                  // NOLINT(readability-identifier-naming)
// using PyObjectSubscriber = srf::Subscriber<pybind11::object>;  // NOLINT(readability-identifier-naming)
// using PyObjectObserver   = srf::Observer<pybind11::object>;    // NOLINT(readability-identifier-naming)
// using PyObjectObservable = srf::Observable<pybind11::object>;  // NOLINT(readability-identifier-naming)
using PySubscription     = rxcpp::subscription;                                // NOLINT(readability-identifier-naming)
using PyObjectObserver   = rxcpp::observer<PyHolder, void, void, void, void>;  // NOLINT(readability-identifier-naming)
using PyObjectSubscriber = rxcpp::subscriber<PyHolder, PyObjectObserver>;      // NOLINT(readability-identifier-naming)
using PyObjectObservable = rxcpp::observable<PyHolder>;                        // NOLINT(readability-identifier-naming)
using PyNode             = srf::segment::ObjectProperties;                     // NOLINT(readability-identifier-naming)
// NOLINTNEXTLINE(readability-identifier-naming)
// using PyObjectOperateFn = std::function<PySubscription(PyObjectObservable& source, PyObjectSubscriber& subscriber)>;
using PyObjectOperateFn = std::function<PyObjectObservable(PyObjectObservable source)>;  // NOLINT

}  // namespace srf::pysrf

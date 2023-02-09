/**
 * SPDX-FileCopyrightText: Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "mrc/coroutines/scheduler.hpp"
#include "mrc/coroutines/sync_wait.hpp"
#include "mrc/coroutines/task_container.hpp"
#include "mrc/ops/api.hpp"
#include "mrc/ops/controller.hpp"

#include <map>

namespace mrc::ops {

class Namespace
{
  public:
};

class Entry
{
  public:
    Entry(std::string name, std::shared_ptr<IOperator> op, std::shared_ptr<Controller> controller) :
      m_name(validate_name(to_lower(std::move(name)))),
      m_operator(std::move(op)),
      m_controller(std::move(controller))
    {
        CHECK(m_operator);
    }

    const std::string& name() const
    {
        return m_name;
    }

  private:
    static std::string to_lower(std::string&& str)
    {
        std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) {
            return std::tolower(c);
        });
        return str;
    }

    static std::string validate_name(const std::string& name)
    {
        if (!is_valid_name(name))
        {
            throw std::invalid_argument(
                "invalid string used in namespace/entry; must be alphanumeric without special characters");
        }
        return name;
    }

    static bool is_valid_name(const std::string& name)
    {
        return std::all_of(name.begin(), name.end(), [](char c) {
            return std::isalnum(c);
        });
    }

    const std::string m_name;
    const std::shared_ptr<IOperator> m_operator;
    const std::shared_ptr<Controller> m_controller;
};

class Manager
{
  public:
    Manager(std::shared_ptr<coroutines::Scheduler> scheduler) :
      m_scheduler(std::move(scheduler)),
      m_task_container(m_scheduler)
    {}

    ~Manager()
    {
        coroutines::sync_wait(m_task_container.garbage_collect_and_yield_until_empty());
    }

    void register_operator(std::string name, std::shared_ptr<IOperator> op)
    {
        std::lock_guard lock(m_mutex);

        CHECK(op);
        CHECK(!m_entries.contains(name));

        auto controller = std::make_shared<Controller>(*m_scheduler, op->is_stoppable());
        m_entries[name] = std::make_unique<Entry>(name, op, controller);
        m_task_container.start(op->main(controller));
    }

  private:
    std::shared_ptr<coroutines::Scheduler> m_scheduler;
    coroutines::TaskContainer m_task_container;
    std::unordered_map<std::string, std::unique_ptr<Entry>> m_entries;
    std::mutex m_mutex;
};

}  // namespace mrc::ops

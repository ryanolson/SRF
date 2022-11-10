# SPDX-FileCopyrightText: Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import asyncio

import srf
import srf.tests.test_edges_cpp as m


def make_options() -> srf.Options:
    options = srf.Options()
    options.topology.user_cpuset = "0-3"
    options.topology.restrict_gpus = True
    return options


def make_pipeline():

    def gen_data():
        yield 1
        yield 2
        yield 3

    def init1(builder: srf.Builder):
        source = builder.make_source("source", gen_data)
        egress = builder.get_egress("my_int2")

        builder.make_edge(source, egress)

    def init2(builder: srf.Builder):
        ingress = builder.get_ingress("my_int2")
        egress = builder.get_egress("my_int3")

        builder.make_edge(ingress, egress)

    def init3(builder: srf.Builder):
        ingress = builder.get_ingress("my_int3")
        egress = builder.get_egress("my_int4")

        builder.make_edge(ingress, egress)

    def init4(builder: srf.Builder):

        def on_next(data):
            print("Got value: {}".format(data))

        def on_error():
            pass

        def on_complete():
            pass

        ingress = builder.get_ingress("my_int4")
        sink = builder.make_sink("sink", on_next, on_error, on_complete)

        builder.make_edge(ingress, sink)

    pipeline = srf.Pipeline()

    pipeline.make_segment("seg_1", [], [("my_int2", int, False)], init1)
    pipeline.make_segment("seg_2", [("my_int2", int, False)], [("my_int3", int, False)], init2)
    pipeline.make_segment("seg_3", [("my_int3", int, False)], [("my_int4", int, False)], init3)
    pipeline.make_segment("seg_4", [("my_int4", int, False)], [], init4)

    return pipeline


def test_multinode():

    options_1 = make_options()
    options_2 = make_options()
    options_1.architect_url = "127.0.0.1:13337"
    options_1.enable_server = True
    options_1.config_request = "seg_1,seg_4"

    options_2.architect_url = "127.0.0.1:13337"
    # options_2.topology.user_cpuset = "1"
    options_2.config_request = "seg_2,seg_3"

    machine_1 = srf.Executor(options_1)
    machine_2 = srf.Executor(options_2)

    pipeline_1 = make_pipeline()
    pipeline_2 = make_pipeline()

    machine_1.register_pipeline(pipeline_1)
    machine_2.register_pipeline(pipeline_2)

    async def run_executor(exec: srf.Executor):
        exec.start()

        await exec.join_async()

    async def run_async():

        await asyncio.gather(run_executor(machine_1), run_executor(machine_2))

    asyncio.run(run_async())


if (__name__ == "__main__"):
    test_multinode()

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
import ctypes
import os
import signal
from multiprocessing import Array
from multiprocessing import Manager
from multiprocessing import Process
from multiprocessing import Value

import srf
import srf.tests.test_edges_cpp as m


def _set_pdeathsig(sig=signal.SIGTERM):
    """
    Helper function to ensure once parent process exits, its child processes will automatically die
    """

    def prctl_fn():
        libc = ctypes.CDLL("libc.so.6")
        return libc.prctl(1, sig)

    return prctl_fn


os.register_at_fork(after_in_child=_set_pdeathsig(signal.SIGTERM))


def make_options() -> srf.Options:
    options = srf.Options()
    options.topology.user_cpuset = "0-3"
    options.topology.restrict_gpus = True
    return options


def make_pipeline(output_array: list = None):

    if (output_array is None):
        output_array = list()

    def gen_data():
        yield 1
        yield 2
        yield 3

    def init1(builder: srf.Builder):
        print("Starting seg_1")

        source = builder.make_source("source", gen_data)
        egress = builder.get_egress("my_int2")

        builder.make_edge(source, egress)

    def init2(builder: srf.Builder):
        print("Starting seg_2")

        ingress = builder.get_ingress("my_int2")
        egress = builder.get_egress("my_int3")

        builder.make_edge(ingress, egress)

    def init3(builder: srf.Builder):
        print("Starting seg_3")

        ingress = builder.get_ingress("my_int3")
        egress = builder.get_egress("my_int4")

        builder.make_edge(ingress, egress)

    def init4(builder: srf.Builder):
        print("Starting seg_4")

        def on_next(data):
            print("Got value: {}".format(data))
            output_array.append(data)

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


async def run_executor(enable_server: bool, config_request: str, output_array: list):
    options = make_options()

    options.architect_url = "127.0.0.1:13337"
    options.enable_server = enable_server
    options.config_request = config_request

    machine = srf.Executor(options)

    pipeline = make_pipeline(output_array=output_array)

    machine.register_pipeline(pipeline)

    print("Starting executor")

    machine.start()

    await machine.join_async()

    print("Executor completed")


def run_full_pipeline(enable_server: bool, config_request: str, output_array: list):

    asyncio.run(run_executor(enable_server, config_request, output_array))


def test_singlenode():

    output_array = list()

    asyncio.run(run_executor(True, "seg_1,seg_2,seg_3,seg_4", output_array))

    assert output_array == [1, 2, 3]


def test_multinode():

    output_array = list()

    async def run_async():

        await asyncio.gather(run_executor(True, "seg_1,seg_4", output_array),
                             run_executor(False, "seg_2,seg_3", output_array))

    asyncio.run(run_async())

    assert output_array == [1, 2, 3]


def test_singlenode_multiprocess():

    with Manager() as manager:
        output_array = manager.list()

        process_1 = Process(target=run_full_pipeline,
                            kwargs={
                                "enable_server": True,
                                "config_request": "seg_1,seg_2,seg_3,seg_4",
                                "output_array": output_array
                            },
                            name="Executor1")

        # Start executor 1
        process_1.start()

        process_1.join()

        assert list(output_array) == [1, 2, 3]


def test_multinode_multiprocess():

    with Manager() as manager:
        output_array = manager.list()

        process_1 = Process(target=run_full_pipeline,
                            kwargs={
                                "enable_server": True, "config_request": "seg_1,seg_4", "output_array": output_array
                            },
                            name="Executor1")

        process_2 = Process(target=run_full_pipeline,
                            kwargs={
                                "enable_server": False, "config_request": "seg_2,seg_3", "output_array": output_array
                            },
                            name="Executor2")

        # Start executor 1
        process_1.start()

        # Start executor 2
        process_2.start()

        process_1.join()

        process_2.join()

        assert list(output_array) == [1, 2, 3]


if (__name__ == "__main__"):
    test_multinode()

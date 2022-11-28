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
import functools
import os
import signal
import time
from multiprocessing import Array
from multiprocessing import Manager
from multiprocessing import Process
from multiprocessing import Value

import srf
from srf.tests.utils import throw_cpp_error
import srf.tests.test_edges_cpp as m


def pairwise(t):
    it = iter(t)
    return zip(it, it)


node_fn_type = typing.Callable[[srf.Builder], srf.SegmentObject]


@pytest.fixture
def source_pyexception():

    def build(builder: srf.Builder):

        def gen_data_and_raise():
            yield 1
            yield 2
            yield 3

            raise RuntimeError("Raised python error")

        return builder.make_source("source", gen_data_and_raise)

    return build


@pytest.fixture
def source_cppexception():

    def build(builder: srf.Builder):

        def gen_data_and_raise():
            yield 1
            yield 2
            yield 3

            throw_cpp_error()

        return builder.make_source("source", gen_data_and_raise)

    return build


@pytest.fixture
def sink():

    def build(builder: srf.Builder):

        def sink_on_next(data):
            print("Got value: {}".format(data))

        return builder.make_sink("sink", sink_on_next, None, None)

    return build


@pytest.fixture
def build_pipeline():

    def inner(*node_fns: node_fn_type):

        def init_segment(builder: srf.Builder):

            created_nodes = []

            # Loop over node creation functions
            for n in node_fns:
                created_nodes.append(n(builder))

            # For each pair, call make_edge
            for source, sink in pairwise(created_nodes):
                builder.make_edge(source, sink)

        pipe = srf.Pipeline()

        pipe.make_segment("TestSegment11", init_segment)

        return pipe

    return inner


build_pipeline_type = typing.Callable[[typing.Tuple[node_fn_type, ...]], srf.Pipeline]


@pytest.fixture
def build_executor():

    def inner(pipe: srf.Pipeline):
        options = srf.Options()

        executor = srf.Executor(options)
        executor.register_pipeline(pipe)

        executor.start()

        return executor

    return inner


build_executor_type = typing.Callable[[srf.Pipeline], srf.Executor]


def test_pyexception_in_source(source_pyexception: node_fn_type,
                               sink: node_fn_type,
                               build_pipeline: build_pipeline_type,
                               build_executor: build_executor_type):

    pipe = build_pipeline(source_pyexception, sink)

    executor = build_executor(pipe)

    with pytest.raises(RuntimeError):
        executor.join()


def test_cppexception_in_source(source_cppexception: node_fn_type,
                                sink: node_fn_type,
                                build_pipeline: build_pipeline_type,
                                build_executor: build_executor_type):

    pipe = build_pipeline(source_cppexception, sink)

    executor = build_executor(pipe)

    with pytest.raises(RuntimeError):
        executor.join()


def test_pyexception_in_source_async(source_pyexception: node_fn_type,
                                     sink: node_fn_type,
                                     build_pipeline: build_pipeline_type,
                                     build_executor: build_executor_type):

    pipe = build_pipeline(source_pyexception, sink)

    async def run_pipeline():
        executor = build_executor(pipe)

        with pytest.raises(RuntimeError):
            await executor.join_async()

    asyncio.run(run_pipeline())


def test_cppexception_in_source_async(source_cppexception: node_fn_type,
                                      sink: node_fn_type,
                                      build_pipeline: build_pipeline_type,
                                      build_executor: build_executor_type):

    pipe = build_pipeline(source_cppexception, sink)

    async def run_pipeline():
        executor = build_executor(pipe)

        with pytest.raises(RuntimeError):
            await executor.join_async()

    asyncio.run(run_pipeline())


def _set_pdeathsig(sig=signal.SIGTERM):
    """
    Helper function to ensure once parent process exits, its child processes will automatically die
    """

    def prctl_fn():
        libc = ctypes.CDLL("libc.so.6")
        return libc.prctl(1, sig)

    return prctl_fn


os.register_at_fork(after_in_child=_set_pdeathsig(signal.SIGTERM))

global last_cpu
last_cpu = 0


def make_options() -> srf.Options:
    global last_cpu
    options = srf.Options()
    options.topology.user_cpuset = "{}".format(last_cpu)
    last_cpu += 1
    options.topology.restrict_gpus = True
    return options


def make_pipeline(seg_count: int = 2, output_array: list = None):

    assert seg_count >= 1, "Must create at least one segment"

    if (output_array is None):
        output_array = list()

    pipeline = srf.Pipeline()

    def gen_data():
        yield 1
        yield 2
        yield 3

    def on_next(data):
        print("Got value: {}".format(data))
        output_array.append(data)

    def on_error():
        pass

    def on_complete():
        print(f"Completed {seg_name}")

    def seg_init(builder: srf.Builder, seg_name: str, ingress_name: str, egress_name: str):
        print(f"Starting {seg_name}")

        if (ingress_name is None):
            source = builder.make_source("source", gen_data)
        else:
            source = builder.get_ingress(ingress_name)

        if (egress_name is None):
            sink = builder.make_sink("sink", on_next, None, on_complete)
        else:
            sink = builder.get_egress(egress_name)

        builder.make_edge(source, sink)

    for i in range(seg_count):

        is_first = i == 0
        is_last = i + 1 == seg_count

        seg_name = f"seg_{i}"
        ingress_name = f"my_int{i}" if not is_first else None
        egress_name = f"my_int{i+1}" if not is_last else None

        pipeline.make_segment(
            seg_name, [(ingress_name, int, False)] if ingress_name is not None else [],
            [(egress_name, int, False)] if egress_name is not None else [],
            functools.partial(seg_init, seg_name=seg_name, ingress_name=ingress_name, egress_name=egress_name))

        # if (i == 0):

        #     def init_head(builder: srf.Builder, seg_name: str):
        #         print(f"Starting {seg_name}")

        #         source = builder.make_source("source", gen_data)
        #         egress = builder.get_egress(egress_name)

        #         builder.make_edge(source, egress)

        #     pipeline.make_segment(seg_name, [], [(egress_name, int, False)], functools.partial(init_head, seg_name=seg_name)

        # elif (i + 1 == seg_count):

        #     def init_tail(builder: srf.Builder):
        #         print(f"Starting {seg_name}")

        #         def on_next(data):
        #             print("Got value: {}".format(data))
        #             output_array.append(data)

        #         def on_error():
        #             pass

        #         def on_complete():
        #             print(f"Completed {seg_name}")

        #         ingress = builder.get_ingress(ingress_name)
        #         sink = builder.make_sink("sink", on_next, on_error, on_complete)

        #         builder.make_edge(ingress, sink)

        #     pipeline.make_segment(seg_name, [(ingress_name, int, False)], [], init_tail)

        # else:

        #     def init_middle(builder: srf.Builder):
        #         print(f"Starting {seg_name}")

        #         ingress = builder.get_ingress(ingress_name)
        #         egress = builder.get_egress(egress_name)

        #         builder.make_edge(ingress, egress)

        #     pipeline.make_segment(seg_name, [(ingress_name, int, False)], [(egress_name, int, False)], init_middle)

    return pipeline


async def run_executor(enable_server: bool, seg_count: int, config_request: str, output_array: list):
    print("Entering run_executor")

    options = make_options()

    options.architect_url = "127.0.0.1:13337"
    options.enable_server = enable_server
    options.config_request = config_request

    machine = srf.Executor(options)

    pipeline = make_pipeline(seg_count=seg_count, output_array=output_array)

    machine.register_pipeline(pipeline)

    print("Starting executor")

    machine.start()

    print("Executor started. Beginning await")

    await machine.join_async()

    print("Executor completed")


def build_executor(enable_server: bool, seg_count: int, config_request: str, output_array: list):
    print("Entering run_executor")

    options = make_options()

    options.architect_url = "127.0.0.1:13337"
    options.enable_server = enable_server
    options.config_request = config_request

    machine = srf.Executor(options)

    pipeline = make_pipeline(seg_count=seg_count, output_array=output_array)

    machine.register_pipeline(pipeline)

    print("Starting executor")

    machine.start()

    print("Executor started. Beginning await")

    return machine


def run_full_pipeline(enable_server: bool, seg_count: int, config_request: str, output_array: list):

    asyncio.run(
        run_executor(enable_server=enable_server,
                     seg_count=seg_count,
                     config_request=config_request,
                     output_array=output_array))


def test_singlenode():

    seg_count = 2

    output_array = list()

    asyncio.run(
        run_executor(enable_server=True,
                     seg_count=seg_count,
                     config_request=",".join([f"seg_{x}" for x in range(seg_count)]),
                     output_array=output_array))

    time.sleep(1)

    assert output_array == [1, 2, 3]


def test_multinode():

    seg_count = 2

    output_array = list()

    async def run_async():

        exec_1 = build_executor(enable_server=True,
                                seg_count=seg_count,
                                config_request=",".join([f"seg_{x}" for x in range(seg_count) if x % 2 == 0]),
                                output_array=output_array)
        exec_2 = build_executor(enable_server=False,
                                seg_count=seg_count,
                                config_request=",".join([f"seg_{x}" for x in range(seg_count) if x % 2 == 1]),
                                output_array=output_array)

        await exec_2.join_async()
        await exec_1.join_async()

        # # await
        # asyncio.ensure_future(run_executor(True, "seg_1,seg_4", output_array))

        # await asyncio.gather(run_executor(True, "seg_1,seg_4", output_array),
        #                      run_executor(False, "seg_2,seg_3", output_array))

    asyncio.run(run_async())

    assert output_array == [1, 2, 3]


def test_singlenode_multiprocess():

    seg_count = 2

    with Manager() as manager:
        output_array = manager.list()

        process_1 = Process(target=run_full_pipeline,
                            kwargs={
                                "enable_server": True,
                                "seg_count": seg_count,
                                "config_request": ",".join([f"seg_{x}" for x in range(seg_count)]),
                                "output_array": output_array
                            },
                            name="Executor1")

        # Start executor 1
        process_1.start()

        process_1.join()

        assert list(output_array) == [1, 2, 3]


def test_multinode_multiprocess():

    seg_count = 2

    with Manager() as manager:
        output_array = manager.list()

        process_1 = Process(target=run_full_pipeline,
                            kwargs={
                                "enable_server": True,
                                "seg_count": seg_count,
                                "config_request": ",".join([f"seg_{x}" for x in range(seg_count) if x % 2 == 0]),
                                "output_array": output_array
                            },
                            name="Executor1")

        process_2 = Process(target=run_full_pipeline,
                            kwargs={
                                "enable_server": False,
                                "seg_count": seg_count,
                                "config_request": ",".join([f"seg_{x}" for x in range(seg_count) if x % 2 == 1]),
                                "output_array": output_array
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

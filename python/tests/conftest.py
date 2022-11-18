import logging

import pytest


@pytest.fixture(scope="session", autouse=True)
def configure_tests_logging():
    """
    Sets the base logging settings for the entire test suite to ensure logs are generated. Automatically detects if a
    debugger is attached and lowers the logging level to DEBUG.
    """
    import sys

    from srf.core.logging import init_logging

    log_level = logging.WARN

    # Check if a debugger is attached. If so, choose DEBUG for the logging level. Otherwise, only WARN
    trace_func = sys.gettrace()

    if (trace_func is not None):
        trace_module = getattr(trace_func, "__module__", None)

        if (trace_module is not None and trace_module.find("pydevd") != -1):
            log_level = logging.DEBUG

    init_logging("srf-tests", py_level=log_level)


def _wrap_set_log_level(log_level: int):
    from srf.core.logging import get_level
    from srf.core.logging import set_level

    # Save the previous logging level
    old_level = get_level()
    set_level(log_level)

    yield

    set_level(old_level)


@pytest.fixture(scope="function")
def loglevel_debug():
    """
    Sets the logging level to `logging.DEBUG` for this function only.
    """
    _wrap_set_log_level(logging.DEBUG)


@pytest.fixture(scope="function")
def loglevel_info():
    """
    Sets the logging level to `logging.INFO` for this function only.
    """
    _wrap_set_log_level(logging.INFO)


@pytest.fixture(scope="function")
def loglevel_warn():
    """
    Sets the logging level to `logging.WARN` for this function only.
    """
    _wrap_set_log_level(logging.WARN)


@pytest.fixture(scope="function")
def loglevel_error():
    """
    Sets the logging level to `logging.ERROR` for this function only.
    """
    _wrap_set_log_level(logging.ERROR)


@pytest.fixture(scope="function")
def loglevel_fatal():
    """
    Sets the logging level to `logging.FATAL` for this function only.
    """
    _wrap_set_log_level(logging.FATAL)

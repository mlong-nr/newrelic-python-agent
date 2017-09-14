import pytest
import asyncio
import aiohttp.client

from newrelic.api.background_task import background_task
from newrelic.api.external_trace import ExternalTrace
from newrelic.api.transaction import current_transaction

from testing_support.external_fixtures import (create_incoming_headers,
        validate_external_node_params)
from testing_support.fixtures import (override_application_settings,
        validate_transaction_metrics)
from testing_support.mock_external_http_server import (
        MockExternalHTTPHResponseHeadersServer, MockExternalHTTPServer)

from newrelic.packages.six.moves import BaseHTTPServer


@asyncio.coroutine
def fetch(url, headers=None, raise_for_status=False):
    session = aiohttp.client.ClientSession(raise_for_status=raise_for_status)
    request = session._request('GET', url, headers=headers)
    headers = {}

    try:
        response = yield from request
    except aiohttp.client_exceptions.ClientResponseError:
        return headers

    response_text = yield from response.text()
    for header in response_text.split('\n'):
        if not header:
            continue
        try:
            h, v = header.split(':', 1)
        except ValueError:
            continue
        headers[h.strip()] = v.strip()
    session.close()
    return headers


@pytest.fixture(scope='module')
def mock_header_server():
    with MockExternalHTTPHResponseHeadersServer():
        yield


@pytest.mark.parametrize('cat_enabled', [True, False])
def test_outbound_cross_process_headers(cat_enabled, mock_header_server):

    @override_application_settings(
            {'cross_application_tracer.enabled': cat_enabled})
    @background_task()
    def task_test():
        loop = asyncio.get_event_loop()
        headers = loop.run_until_complete(fetch('http://localhost:8989'))

        transaction = current_transaction()
        expected_headers = ExternalTrace.generate_request_headers(transaction)

        for expected_header, expected_value in expected_headers:
            assert headers.get(expected_header) == expected_value

        if not cat_enabled:
            assert not headers.get(ExternalTrace.cat_id_key)
            assert not headers.get(ExternalTrace.cat_transaction_key)

    task_test()


_nr_key = ExternalTrace.cat_id_key
_customer_headers_tests = [
        {'MY_CUSTOM_HEADER': 'I love cats'},
        {_nr_key: 'I love dogs'},
]


@pytest.mark.parametrize('customer_headers', _customer_headers_tests)
@background_task()
def test_outbound_cross_process_headers_custom_headers(customer_headers,
        mock_header_server):

    loop = asyncio.get_event_loop()
    headers = loop.run_until_complete(fetch('http://localhost:8989',
        customer_headers.copy()))

    # always honor customer headers
    for expected_header, expected_value in customer_headers.items():
        assert headers.get(expected_header) == expected_value


def test_outbound_cross_process_headers_no_txn(mock_header_server):

    loop = asyncio.get_event_loop()
    headers = loop.run_until_complete(fetch('http://localhost:8989'))

    assert not headers.get(ExternalTrace.cat_id_key)
    assert not headers.get(ExternalTrace.cat_transaction_key)


@background_task()
def test_outbound_cross_process_headers_exception(mock_header_server):

    # corrupt the transaction object to force an error
    transaction = current_transaction()
    guid = transaction.guid
    delattr(transaction, 'guid')

    try:
        loop = asyncio.get_event_loop()
        headers = loop.run_until_complete(fetch('http://localhost:8989'))

        assert not headers.get(ExternalTrace.cat_id_key)
        assert not headers.get(ExternalTrace.cat_transaction_key)
    finally:
        transaction.guid = guid


@pytest.mark.parametrize('cat_enabled', [True, False])
@pytest.mark.parametrize('response_code', [200, 404])
@pytest.mark.parametrize('raise_for_status', [True, False])
def test_process_incoming_headers(cat_enabled, response_code,
        raise_for_status):

    _test_cross_process_response_scoped_metrics = [
            ('ExternalTransaction/localhost:8990/1#2/test', 1 if cat_enabled
                else None)]

    _test_cross_process_response_rollup_metrics = [
            ('External/all', 1),
            ('External/allOther', 1),
            ('External/localhost:8990/all', 1),
            ('ExternalApp/localhost:8990/1#2/all', 1 if cat_enabled else None),
            ('ExternalTransaction/localhost:8990/1#2/test', 1 if cat_enabled
                else None)]

    _test_cross_process_response_external_node_params = [
            ('cross_process_id', '1#2'),
            ('external_txn_name', 'test'),
            ('transaction_guid', '0123456789012345')]

    _test_cross_process_response_external_node_forgone_params = [
            k for k, v in _test_cross_process_response_external_node_params]

    @override_application_settings(
            {'cross_application_tracer.enabled': cat_enabled})
    @validate_transaction_metrics(
            'test_cat:test_process_incoming_headers.<locals>.task_test',
            scoped_metrics=_test_cross_process_response_scoped_metrics,
            rollup_metrics=_test_cross_process_response_rollup_metrics,
            background_task=True)
    @validate_external_node_params(
            params=(_test_cross_process_response_external_node_params if
                cat_enabled else []),
            forgone_params=([] if cat_enabled else
                _test_cross_process_response_external_node_forgone_params))
    @background_task()
    def task_test():
        transaction = current_transaction()
        headers = create_incoming_headers(transaction)

        def do_GET(self):
            self.send_response(response_code)
            for header, value in headers:
                self.send_header(header, value)
            self.end_headers()
            self.wfile.write(b'')

        handler = type('ResponseHandler',
                (BaseHTTPServer.BaseHTTPRequestHandler,), {'do_GET': do_GET})

        with MockExternalHTTPServer(handler=handler, port=8990):
            loop = asyncio.get_event_loop()
            loop.run_until_complete(fetch('http://localhost:8990',
                raise_for_status=raise_for_status))

    task_test()

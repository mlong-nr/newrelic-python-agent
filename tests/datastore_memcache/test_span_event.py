import pytest
import memcache

from newrelic.api.transaction import current_transaction
from testing_support.fixtures import override_application_settings
from testing_support.validators.validate_span_events import (
        validate_span_events)
from testing_support.settings import memcached_multiple_settings
from testing_support.util import instance_hostname

from newrelic.api.background_task import background_task

DB_SETTINGS = memcached_multiple_settings()[0]
MEMCACHED_ADDR = '%s:%s' % (DB_SETTINGS['host'], DB_SETTINGS['port'])

# Settings

_enable_instance_settings = {
    'datastore_tracer.instance_reporting.enabled': True,
    'distributed_tracing.enabled': True,
    'span_events.enabled': True,
}
_disable_instance_settings = {
    'datastore_tracer.instance_reporting.enabled': False,
    'distributed_tracing.enabled': True,
    'span_events.enabled': True,
}


# Query

def _exercise_db(client):
    key = DB_SETTINGS['namespace'] + 'key'
    client.set(key, 'value')
    value = client.get(key)
    client.delete(key)
    assert value == 'value'


# Tests

@pytest.mark.parametrize('instance_enabled', (True, False))
def test_span_events(instance_enabled):
    guid = 'dbb533c53b749e0b'
    priority = 0.5

    common = {
        'type': 'Span',
        'transactionId': guid,
        'priority': priority,
        'sampled': True,
        'category': 'datastore',
        'component': 'Memcached',
        'span.kind': 'client',
    }

    exact_agents = {}
    if instance_enabled:
        settings = _enable_instance_settings
        hostname = instance_hostname(DB_SETTINGS['host'])
        exact_agents.update({
            'peer.address': '%s:%s' % (hostname, DB_SETTINGS['port']),
            'peer.hostname': hostname,
        })
    else:
        settings = _disable_instance_settings
        exact_agents.update({
            'peer.address': 'Unknown:Unknown',
            'peer.hostname': 'Unknown',
        })

    query_1 = common.copy()
    query_1['name'] = 'Datastore/operation/Memcached/set'

    query_2 = common.copy()
    query_2['name'] = 'Datastore/operation/Memcached/get'

    query_3 = common.copy()
    query_3['name'] = 'Datastore/operation/Memcached/delete'

    @validate_span_events(count=1, exact_intrinsics=query_1,
            exact_agents=exact_agents,
            unexpected_agents=('db.instance',))
    @validate_span_events(count=1, exact_intrinsics=query_2,
            exact_agents=exact_agents,
            unexpected_agents=('db.instance',))
    @validate_span_events(count=1, exact_intrinsics=query_3,
            exact_agents=exact_agents,
            unexpected_agents=('db.instance',))
    @validate_span_events(count=0, expected_intrinsics=('db.statement',))
    @override_application_settings(settings)
    @background_task(name='span_events')
    def _test():
        txn = current_transaction()
        txn.guid = guid
        txn._priority = priority
        txn._sampled = True

        client = memcache.Client([MEMCACHED_ADDR])
        _exercise_db(client)

    _test()
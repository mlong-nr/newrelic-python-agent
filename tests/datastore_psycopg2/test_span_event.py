import pytest
import psycopg2

from newrelic.api.transaction import current_transaction
from testing_support.fixtures import override_application_settings
from testing_support.validators.validate_span_events import (
        validate_span_events)
from testing_support.util import instance_hostname
from utils import DB_SETTINGS

from newrelic.api.background_task import background_task


# Settings

_enable_instance_settings = {
    'datastore_tracer.instance_reporting.enabled': True,
    'datastore_tracer.database_name_reporting.enabled': True,
    'distributed_tracing.enabled': True,
    'span_events.enabled': True,
}
_disable_instance_settings = {
    'datastore_tracer.instance_reporting.enabled': False,
    'datastore_tracer.database_name_reporting.enabled': False,
    'distributed_tracing.enabled': True,
    'span_events.enabled': True,
}


def _exercise_db():
    connection = psycopg2.connect(
            database=DB_SETTINGS['name'], user=DB_SETTINGS['user'],
            password=DB_SETTINGS['password'], host=DB_SETTINGS['host'],
            port=DB_SETTINGS['port'])

    try:
        cursor = connection.cursor()
        cursor.execute("""SELECT setting from pg_settings where name=%s""",
                ('server_version',))

        # No target
        cursor.execute('SELECT 1')
    finally:
        connection.close()


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
        'component': 'Postgres',
        'span.kind': 'client',
    }

    if instance_enabled:
        settings = _enable_instance_settings
        hostname = instance_hostname(DB_SETTINGS['host'])
        common.update({
            'db.instance': DB_SETTINGS['name'],
            'peer.address': '%s:%s' % (hostname, DB_SETTINGS['port']),
            'peer.hostname': hostname,
        })
    else:
        settings = _disable_instance_settings
        common.update({
            'db.instance': 'Unknown',
            'peer.address': 'Unknown:Unknown',
            'peer.hostname': 'Unknown',
        })

    query_1 = common.copy()
    query_1['name'] = 'Datastore/statement/Postgres/pg_settings/select'
    query_1['db.statement'] = 'SELECT setting from pg_settings where name=%s'

    query_2 = common.copy()
    query_2['name'] = 'Datastore/operation/Postgres/select'
    query_2['db.statement'] = 'SELECT ?'

    @validate_span_events(count=1, exact_intrinsics=query_1)
    @validate_span_events(count=1, exact_intrinsics=query_2)
    @override_application_settings(settings)
    @background_task(name='span_events')
    def _test():
        txn = current_transaction()
        txn.guid = guid
        txn._priority = priority
        txn._sampled = True
        _exercise_db()

    _test()
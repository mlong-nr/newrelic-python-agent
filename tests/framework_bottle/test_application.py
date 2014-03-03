import pytest
import base64

from testing_support.fixtures import (validate_transaction_metrics,
    validate_transaction_errors)

from newrelic.packages import six

import webtest

from bottle import __version__ as version

version = [int(x) for x in version.split('-')[0].split('.')]

if len(version) == 2:
    version.append(0)

version = tuple(version)

requires_auth_basic = pytest.mark.skipif(version < (0, 9, 0),
        reason="Bottle only added auth_basic in 0.9.0.")

_test_application_index_scoped_metrics = [
        ('Python/WSGI/Application', 1),
        ('Python/WSGI/Response', 1),
        ('Python/WSGI/Finalize', 1),
        ('Function/_target_application:index_page', 1)]

if version >= (0, 9, 0):
    _test_application_index_scoped_metrics.extend([
        ('Function/bottle:Bottle.wsgi', 1)])
else:
    _test_application_index_scoped_metrics.extend([
        ('Function/bottle:Bottle.__call__', 1)])

_test_application_index_custom_metrics = [
        ('Python/Framework/Bottle/%s.%s.%s' % version, 1)]

@validate_transaction_errors(errors=[])
@validate_transaction_metrics('_target_application:index_page',
        scoped_metrics=_test_application_index_scoped_metrics,
        custom_metrics=_test_application_index_custom_metrics)
def test_application_index(target_application):
    response = target_application.get('/index')
    response.mustcontain('INDEX RESPONSE')

_test_application_error_scoped_metrics = [
        ('Python/WSGI/Application', 1),
        ('Python/WSGI/Response', 1),
        ('Python/WSGI/Finalize', 1),
        ('Function/_target_application:error_page', 1)]

if version >= (0, 9, 0):
    _test_application_error_scoped_metrics.extend([
        ('Function/bottle:Bottle.wsgi', 1)])
else:
    _test_application_error_scoped_metrics.extend([
        ('Function/bottle:Bottle.__call__', 1)])

_test_application_error_custom_metrics = [
        ('Python/Framework/Bottle/%s.%s.%s' % version, 1)]

if six.PY3:
    _test_application_error_errors = ['builtins:RuntimeError']
else:
    _test_application_error_errors = ['exceptions:RuntimeError']

@validate_transaction_errors(errors=_test_application_error_errors)
@validate_transaction_metrics('_target_application:error_page',
        scoped_metrics=_test_application_error_scoped_metrics,
        custom_metrics=_test_application_error_custom_metrics)
def test_application_error(target_application):
    response = target_application.get('/error', status=500, expect_errors=True)

_test_application_not_found_scoped_metrics = [
        ('Python/WSGI/Application', 1),
        ('Python/WSGI/Response', 1),
        ('Python/WSGI/Finalize', 1),
        ('Function/_target_application:error404_page', 1)]

if version >= (0, 9, 0):
    _test_application_not_found_scoped_metrics.extend([
        ('Function/bottle:Bottle.wsgi', 1)])
else:
    _test_application_not_found_scoped_metrics.extend([
        ('Function/bottle:Bottle.__call__', 1)])

_test_application_not_found_custom_metrics = [
        ('Python/Framework/Bottle/%s.%s.%s' % version, 1)]

@validate_transaction_errors(errors=[])
@validate_transaction_metrics('_target_application:error404_page',
        scoped_metrics=_test_application_not_found_scoped_metrics,
        custom_metrics=_test_application_not_found_custom_metrics)
def test_application_not_found(target_application):
    response = target_application.get('/missing', status=404)
    response.mustcontain('NOT FOUND')

_test_application_auth_basic_fail_scoped_metrics = [
        ('Python/WSGI/Application', 1),
        ('Python/WSGI/Response', 1),
        ('Python/WSGI/Finalize', 1),
        ('Function/_target_application:auth_basic_page', 1)]

if version >= (0, 9, 0):
    _test_application_auth_basic_fail_scoped_metrics.extend([
        ('Function/bottle:Bottle.wsgi', 1)])
else:
    _test_application_auth_basic_fail_scoped_metrics.extend([
        ('Function/bottle:Bottle.__call__', 1)])

_test_application_auth_basic_fail_custom_metrics = [
        ('Python/Framework/Bottle/%s.%s.%s' % version, 1)]

@requires_auth_basic
@validate_transaction_errors(errors=[])
@validate_transaction_metrics('_target_application:auth_basic_page',
        scoped_metrics=_test_application_auth_basic_fail_scoped_metrics,
        custom_metrics=_test_application_auth_basic_fail_custom_metrics)
def test_application_auth_basic_fail(target_application):
    response = target_application.get('/auth', status=401)

_test_application_auth_basic_okay_scoped_metrics = [
        ('Python/WSGI/Application', 1),
        ('Python/WSGI/Response', 1),
        ('Python/WSGI/Finalize', 1),
        ('Function/_target_application:auth_basic_page', 1)]

if version >= (0, 9, 0):
    _test_application_auth_basic_okay_scoped_metrics.extend([
        ('Function/bottle:Bottle.wsgi', 1)])
else:
    _test_application_auth_basic_okay_scoped_metrics.extend([
        ('Function/bottle:Bottle.__call__', 1)])

_test_application_auth_basic_okay_custom_metrics = [
        ('Python/Framework/Bottle/%s.%s.%s' % version, 1)]

@requires_auth_basic
@validate_transaction_errors(errors=[])
@validate_transaction_metrics('_target_application:auth_basic_page',
        scoped_metrics=_test_application_auth_basic_okay_scoped_metrics,
        custom_metrics=_test_application_auth_basic_okay_custom_metrics)
def test_application_auth_basic_okay(target_application):
    authorization_value = base64.b64encode(b'user:password')
    if six.PY3:
        authorization_value = authorization_value.decode('Latin-1')
    environ = { 'HTTP_AUTHORIZATION': 'Basic ' + authorization_value }
    response = target_application.get('/auth', extra_environ=environ)
    response.mustcontain('AUTH OKAY')
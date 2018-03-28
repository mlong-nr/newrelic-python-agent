import webtest

from flask import Flask
from flask_restplus import Resource, Api
from werkzeug.exceptions import HTTPException


app = Flask(__name__)
api = Api(app)


class IndexResource(Resource):
    def get(self):
        return {'hello': 'world'}


class CustomException(Exception):
    pass


class ExceptionResource(Resource):
    def get(self, exception, code):
        if 'HTTPException' in exception:
            e = HTTPException()
        elif 'CustomException' in exception:
            e = CustomException()
        else:
            raise AssertionError('Unexpected exception received: %s' %
                    exception)
        e.code = code
        raise e


api.add_resource(IndexResource, '/index')
api.add_resource(ExceptionResource, '/exception/<string:exception>/<int:code>')

test_application = webtest.TestApp(app)

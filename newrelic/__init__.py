version = '1.1.1'

try:
    from newrelic.build import build_number
except:
    build_number = 0

version_info = map(int, version.split('.')) + [build_number]
version = '.'.join(map(str, version_info))

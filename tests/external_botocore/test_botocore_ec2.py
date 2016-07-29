import uuid

import botocore.session
import moto

from newrelic.agent import background_task
from testing_support.fixtures import validate_transaction_metrics

AWS_ACCESS_KEY_ID = 'AAAAAAAAAAAACCESSKEY'
AWS_SECRET_ACCESS_KEY = 'AAAAAASECRETKEY'
AWS_REGION = 'us-east-1'

TEST_INSTANCE = 'python-agent-test-%s' % uuid.uuid4()

_ec2_scoped_metrics = [
    ('External/ec2.us-east-1.amazonaws.com/botocore/POST', 3),
]

_ec2_rollup_metrics = [
    ('External/all', 3),
    ('External/allOther', 3),
    ('External/ec2.us-east-1.amazonaws.com/all', 3),
    ('External/ec2.us-east-1.amazonaws.com/botocore/POST', 3),
]

@validate_transaction_metrics(
        'test_botocore_ec2:test_ec2',
        scoped_metrics=_ec2_scoped_metrics,
        rollup_metrics=_ec2_rollup_metrics,
        background_task=True)
@background_task()
@moto.mock_ec2
def test_ec2():
    session = botocore.session.get_session()
    client = session.create_client(
            'ec2',
            region_name=AWS_REGION,
            aws_access_key_id=AWS_ACCESS_KEY_ID,
            aws_secret_access_key=AWS_SECRET_ACCESS_KEY
    )

    # Create 1 instance
    resp = client.run_instances(
            ImageId='',
            MinCount=1,
            MaxCount=1,
    )
    assert resp['ResponseMetadata']['HTTPStatusCode'] == 200
    assert len(resp['Instances']) == 1
    instance_id = resp['Instances'][0]['InstanceId']

    # Update the instance type
    resp = client.modify_instance_attribute(
            InstanceId=instance_id,
            Attribute='instanceType',
            InstanceType={'Value': 'i2.4xlarge'},
    )
    assert resp['ResponseMetadata']['HTTPStatusCode'] == 200

    # Delete instance
    resp = client.terminate_instances(InstanceIds=[instance_id])
    assert resp['ResponseMetadata']['HTTPStatusCode'] == 200
    assert resp['TerminatingInstances'][0]['InstanceId'] == instance_id

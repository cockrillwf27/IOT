import json
import boto3

dynamodb = boto3.resource('dynamodb')
table = dynamodb.Table('ee419_lab')

def lambda_handler(event, context):
    print("unregister event:", json.dumps(event))
    device_id = event.get('deviceId')
    if not device_id:
        return {'statusCode': 400, 'body': 'missing deviceId'}

    resp = table.query(
        IndexName='DeviceId',
        KeyConditionExpression='deviceId = :d',
        ExpressionAttributeValues={':d': device_id},
        Limit=1
    )
    items = resp.get('Items', [])
    if not items:
        print(f"{device_id} not registered")
        return {'statusCode': 404, 'body': 'not registered'}

    nfc_tag = items[0]['nfcTag']
    table.update_item(
        Key={'nfcTag': nfc_tag},
        UpdateExpression='SET deviceId = :d, inPlace = :i',
        ExpressionAttributeValues={':d': 'None', ':i': 0}
    )
    print(f"unregistered {device_id}, freed {nfc_tag}")
    return {'statusCode': 200, 'body': f'freed {nfc_tag}'}

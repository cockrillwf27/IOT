import json
import boto3

dynamodb = boto3.resource('dynamodb')
table = dynamodb.Table('ee419_lab')

def lambda_handler(event, context):
    print("status event:", json.dumps(event))
    device_id = event.get('deviceId')
    in_place = event.get('inPlace')
    if not device_id or in_place is None:
        return {'statusCode': 400, 'body': 'missing deviceId or inPlace'}

    place_val = 1 if in_place else 0

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
        UpdateExpression='SET inPlace = :i',
        ExpressionAttributeValues={':i': place_val}
    )
    print(f"{device_id} inPlace set to {place_val}")
    return {'statusCode': 200, 'body': f'updated {place_val}'}

import json
import boto3

dynamodb = boto3.resource('dynamodb')
table = dynamodb.Table('ee419_lab')
iot = boto3.client('iot-data', region_name='us-east-2')

def lambda_handler(event, context):
    print("register event:", json.dumps(event))
    device_id = event.get('deviceId')
    if not device_id:
        return {'statusCode': 400, 'body': 'missing deviceId'}

    existing = table.query(
        IndexName='DeviceId',
        KeyConditionExpression='deviceId = :d',
        ExpressionAttributeValues={':d': device_id},
        Limit=1
    )
    if existing.get('Items'):
        print(f"{device_id} already registered")
        return {'statusCode': 409, 'body': 'already registered'}

    free = table.query(
        IndexName='DeviceId',
        KeyConditionExpression='deviceId = :d',
        ExpressionAttributeValues={':d': 'None'},
        Limit=1
    )
    items = free.get('Items', [])
    if not items:
        print("no free tags")
        return {'statusCode': 409, 'body': 'no free tags'}

    item = items[0]
    nfc_tag = item['nfcTag']
    flash_count = int(item.get('bitPosition', 1))

    table.update_item(
        Key={'nfcTag': nfc_tag},
        UpdateExpression='SET deviceId = :d',
        ExpressionAttributeValues={':d': device_id}
    )
    print(f"assigned {nfc_tag} to {device_id} (flashCount={flash_count})")

    payload = json.dumps({"targetTag": nfc_tag, "flashCount": flash_count})
    topic = f"ee419_lab/{device_id}/cmd"
    iot.publish(topic=topic, qos=1, payload=payload)
    print(f"published {topic}: {payload}")

    return {'statusCode': 200, 'body': f'assigned {nfc_tag}'}

# EE-419 Lab 7 — Lambda Functions

AWS Lambda functions that process MQTT messages from ESP32 edge devices and maintain shared system state in DynamoDB.

## Overview

Three serverless functions handle device registration, unregistration, and real-time tag presence updates. They share a single DynamoDB table and an IAM execution role.

| Function            | MQTT Trigger Topic             | Purpose                                              |
|---------------------|--------------------------------|------------------------------------------------------|
| `register_device`   | `ee419_lab/register`           | Assign a free NFC tag and publish the target command |
| `unregister_device` | `ee419_lab/unregister`         | Release a tag and reset its status                   |
| `update_device`     | `ee419_lab/+/status`           | Update `inPlace` from the device’s `targetFound`     |

- **Region:** `us-east-2`
- **Execution role:** `ee419_Role`
- **Runtime:** Python 3.12

## DynamoDB Table

**Table name:** `ee419_lab`  
**Partition key:** `nfcTag` (String)

### Global Secondary Indexes

| Index Name | Partition Key | Type   | Projection   |
|------------|---------------|--------|--------------|
| `DeviceId` | `deviceId`    | String | All          |
| `InPlace`  | `inPlace`     | Number | Keys only    |

### Item Attributes

| Attribute     | Type   | Description                                      |
|---------------|--------|--------------------------------------------------|
| `nfcTag`      | String | Hex UID of the NFC tag (partition key)           |
| `bitPosition` | Number | Flash count value sent to the device (1, 2, 4, 8) |
| `deviceId`    | String | Assigned device ID or `"None"` if free           |
| `inPlace`     | Number | `1` if the target tag is currently present, else `0` |

## Lambda Functions

### register_device

**IoT Rule SQL**
```sql
SELECT deviceId FROM 'ee419_lab/register'
```

**Behavior**
- Rejects the request if the device is already registered (409).
- Queries for the first free tag (`deviceId == "None"`).
- Assigns the tag to the device.
- Publishes a command to `ee419_lab/<deviceId>/cmd`:
  ```json
  {
    "targetTag": "<nfcTag>",
    "flashCount": <bitPosition>
  }
  ```
- Returns 409 if no free tags remain.

### unregister_device

**IoT Rule SQL**
```sql
SELECT deviceId FROM 'ee419_lab/unregister'
```

**Behavior**
- Looks up the tag currently assigned to the device.
- Sets `deviceId` back to `"None"` and `inPlace` to `0`.
- Returns 404 if the device was not registered.

### update_device

**IoT Rule SQL**
```sql
SELECT topic(2) as deviceId, targetFound as inPlace FROM 'ee419_lab/+/status'
```

**Behavior**
- Converts the boolean `targetFound` into a number (`1` or `0`).
- Updates the `inPlace` attribute on the device’s assigned tag.
- Returns 404 if the device is not registered.

## MQTT Topics

| Topic                              | Direction      | Payload Example                                      |
|------------------------------------|----------------|------------------------------------------------------|
| `ee419_lab/register`               | Device → Cloud | `{"deviceId": "cockrillwf27-esp32s3"}`              |
| `ee419_lab/unregister`             | Device → Cloud | `{"deviceId": "cockrillwf27-esp32s3"}`              |
| `ee419_lab/<deviceId>/status`      | Device → Cloud | `{"targetFound": true}`                             |
| `ee419_lab/<deviceId>/cmd`         | Cloud → Device | `{"targetTag": "04A1B2C3", "flashCount": 4}`        |

## Testing

### Lambda Console
Create synchronous test events for each function and verify return codes plus DynamoDB changes.

### MQTT Test Client
Subscribe to `ee419_lab/+/cmd` and `ee419_lab/+/status`, then publish register, status, and unregister messages. Confirm table updates and command delivery.

### ESP32 End-to-End
Reset the board, confirm it receives a `cmd` with the correct `flashCount`, and verify LED behavior and `inPlace` updates when the matching card is presented or removed.

## Notes

- Each device is assigned exactly one target tag.
- `flashCount` is taken directly from the tag’s `bitPosition`.
- Device certificates and private keys are excluded from version control.
- All functions share the same IAM role with least-privilege access to the table, indexes, and the `ee419_lab/*/cmd` publish topic.

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

#include "BLEServer.h"

#include <cagattservice.h>  //contains GUIDs defined by IoTivity
#include <logger.h>

#define TAG "BLEServer"



@interface BLEServer () <CBPeripheralManagerDelegate>

@property (strong, nonatomic) CBPeripheralManager *peripheralManager;

@property (strong, nonatomic) NSString* uuid;
@property (strong, nonatomic) CBUUID* serviceUUID;
@property (strong, nonatomic) CBUUID* rxCharacteristicUUID;
@property (strong, nonatomic) CBUUID* txCharacteristicUUID;

@property (strong, nonatomic) CBMutableCharacteristic *rxCharacteristic;
@property (strong, nonatomic) CBMutableCharacteristic *txCharacteristic;

@property (strong, nonatomic) dispatch_semaphore_t initLock;

@end



@implementation BLEServer

-(id)init {
    self = [super init];
    if(self) {
        _uuid                 = [[NSUUID UUID] UUIDString];
        _serviceUUID          = [CBUUID UUIDWithString:@CA_DEFAULT_GATT_SERVICE_UUID];
        _rxCharacteristicUUID = [CBUUID UUIDWithString:@CA_DEFAULT_GATT_RESPONSE_CHRC_UUID];
        _txCharacteristicUUID = [CBUUID UUIDWithString:@CA_DEFAULT_GATT_REQUEST_CHRC_UUID];

        _initLock = dispatch_semaphore_create(0);

        _peripheralManager = [[CBPeripheralManager alloc] initWithDelegate:self queue:nil];

        OIC_LOG_V(INFO, TAG, "Created BLEServer. UUID=%s", [_uuid UTF8String]);


        //now wait until "peripheralManagerDidUpdateState" is called before returning
        //TODO: don't wait FOREVER!
        dispatch_semaphore_wait(_initLock, DISPATCH_TIME_FOREVER);

    }
    return self;
}


-(void)startAdvertising {
    if([self.peripheralManager isAdvertising]) {
        OIC_LOG_V(WARNING, TAG, "%s: already advertising", __FUNCTION__);
        return;
    }

    OIC_LOG_V(INFO, TAG, "%s", __FUNCTION__);

    //TODO: fix this!!! put it somewhere
    #define DEVICE_NAME @"OIC-PERIPH-DEVICE"
    
    [self.peripheralManager startAdvertising:@{
        CBAdvertisementDataServiceUUIDsKey : @[self.serviceUUID],
        CBAdvertisementDataLocalNameKey : DEVICE_NAME
    }];
}

-(void)stopAdvertising {
    if(![self.peripheralManager isAdvertising]) {
        OIC_LOG_V(WARNING, TAG, "%s: not advertising", __FUNCTION__);
        return;
    }

    OIC_LOG_V(INFO, TAG, "%s", __FUNCTION__);
    [self.peripheralManager stopAdvertising];
}


- (void)sendMessage:(const uint8_t *)dataIn dataSize:(uint32_t)dataLen
{
    // Start sending
    NSData* data = [NSData dataWithBytes:(const char*) dataIn length:dataLen];
    NSLog(@"peripheral ------ updateValue(%@) -----> central", data);
    BOOL didSend = false;
    do {
        didSend = [self.peripheralManager updateValue:data forCharacteristic:self.rxCharacteristic onSubscribedCentrals:nil];
        usleep(1000);
    } while (!didSend);
    NSLog(@"Did Send = %d", didSend);
}

-(void)updateCharacteristicsTo:(const char*)address withData:(const uint8_t*)data withLength:(uint32_t)dataLength {
    OIC_LOG_V(INFO, TAG, "%s: address=%s", __FUNCTION__, address);

    [self sendMessage:data dataSize:dataLength];
}

-(void)updateCharacteristicsToAll:(const uint8_t*)data withLength:(uint32_t)dataLength {
    OIC_LOG_V(INFO, TAG, "%s", __FUNCTION__);
}



#pragma mark - CBPeripheralManagerDelegate Implementation

- (void)peripheralManagerDidUpdateState:(CBPeripheralManager *)peripheral {
    OIC_LOG_V(INFO, TAG, "%s", __FUNCTION__);

    if (_peripheralManager.state != CBPeripheralManagerStatePoweredOn) {
        OIC_LOG_V(ERROR, TAG, "Error: the peripheralManager should have CBPeripheralManagerStatePoweredOn (was %ld). Perhaps BLE is disabled!", (long) _peripheralManager.state);
        dispatch_semaphore_signal(self.initLock);
        return;
    }

    // Create Characteristics
    self.rxCharacteristic = [[CBMutableCharacteristic alloc] initWithType:self.rxCharacteristicUUID properties:CBCharacteristicPropertyNotify value:nil permissions:CBAttributePermissionsReadable];
    self.txCharacteristic = [[CBMutableCharacteristic alloc] initWithType:self.txCharacteristicUUID properties:(CBCharacteristicPropertyWrite | CBCharacteristicPropertyWriteWithoutResponse | CBCharacteristicPropertyNotify) value:nil permissions:CBAttributePermissionsWriteable];

    // Create Service
    CBMutableService *oicGattService =  [[CBMutableService alloc] initWithType:self.serviceUUID primary:YES];

    oicGattService.characteristics = @[self.rxCharacteristic, self.txCharacteristic];

    [self.peripheralManager addService:oicGattService];

    OIC_LOG_V(INFO, TAG, "%s: added OIC service to peripheralManager", __FUNCTION__);

    //notify init() function to continue
    dispatch_semaphore_signal(self.initLock);
}

- (void)peripheralManager:(CBPeripheralManager *)peripheral didAddService:(CBService *)service error:(NSError *)error {
    if(error) {
        OIC_LOG_V(ERROR, TAG, "%s - failed", __FUNCTION__);
        NSLog(@"error: %@", error);
    }
    else {
        OIC_LOG_V(INFO, TAG, "%s", __FUNCTION__);
    }
}

- (void)peripheralManagerDidStartAdvertising:(CBPeripheralManager *)peripheral error:(NSError *)error {
    if(error) {
        OIC_LOG_V(ERROR, TAG, "%s - failed", __FUNCTION__);
        NSLog(@"error: %@", error);
    }
    else {
        OIC_LOG_V(INFO, TAG, "%s", __FUNCTION__);
    }
}

- (void)peripheralManager:(CBPeripheralManager *)peripheral didReceiveReadRequest:(CBATTRequest *)request {
    OIC_LOG_V(INFO, TAG, "%s", __FUNCTION__);

    if ([request.characteristic.UUID isEqual:self.rxCharacteristicUUID]) {
        OIC_LOG_V(INFO, TAG, "%s: peripheral <---- ReadRequest ---- central", __FUNCTION__);

        if (request.offset > self.rxCharacteristic.value.length) {
            [self.peripheralManager respondToRequest:request withResult:CBATTErrorInvalidOffset];
            return;
        }
        request.value = [self.rxCharacteristic.value subdataWithRange:NSMakeRange(request.offset, self.rxCharacteristic.value.length - request.offset)];
        OIC_LOG_V(INFO, TAG, "%s: peripheral ---- ReadRequestResponse ----> central", __FUNCTION__);
        [self.peripheralManager respondToRequest:request withResult:CBATTErrorSuccess];
    }
    else {
        NSLog(@"%s: ignoring read request for %@", __FUNCTION__, request.characteristic.UUID);
    }
}

- (void)peripheralManager:(CBPeripheralManager *)peripheral didReceiveWriteRequests:(NSArray<CBATTRequest *> *)requests {
    OIC_LOG_V(INFO, TAG, "%s", __FUNCTION__);

    for (CBATTRequest *request in requests) {
        if (self.dataReceivedCallback) {
            uint32_t sendLength = 0;
            self.dataReceivedCallback([self.uuid UTF8String], (const uint8_t *)[request.value bytes], (uint32_t)[request.value length], &sendLength);
        }
    }
    OIC_LOG_V(INFO, TAG, "%s: peripheral ---- WriteRequestResponse ----> central", __FUNCTION__);
    [self.peripheralManager respondToRequest:[requests objectAtIndex:0] withResult:CBATTErrorSuccess];
}

- (void)peripheralManager:(CBPeripheralManager *)peripheral central:(CBCentral *)central didSubscribeToCharacteristic:(CBCharacteristic *)characteristic {
    OIC_LOG_V(INFO, TAG, "%s", __FUNCTION__);
}

@end



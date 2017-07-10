#include "BLEClient.h"

#include <cagattservice.h>  //contains GUIDs defined by IoTivity
#include <logger.h>

#define TAG "BLEClient"


static CBUUID* g_OICGattServiceUUID = NULL;
static CBUUID* g_rxCharacteristicUUID = NULL;
static CBUUID* g_txCharacteristicUUID = NULL;

typedef void (^PeripheralInitializationCompletionBlock)();

typedef enum : NSUInteger {
    OICDeviceStateDisconnected,
    OICDeviceStateConnected,
    OICDeviceStateReady,
    OICDeviceStateDisconnectRequested,
} OICDeviceState;

@interface OICPeripheral : NSObject<CBPeripheralDelegate>
@property (strong, nonatomic) NSString* address;
@property (strong, nonatomic) CBPeripheral* peripheral;
@property (nonatomic, assign, readwrite) OICDeviceState state;
@property (copy, nonatomic) PeripheralInitializationCompletionBlock initializationCompletionBlock;
@property (nonatomic) CABLEDataReceivedCallback dataReceivedCallback;
@property (strong, nonatomic) CBCharacteristic* rxCharacteristic;
@property (strong, nonatomic) CBCharacteristic* txCharacteristic;
-(id)initWithPeripheral:(CBPeripheral*)peripheral;
-(void)discoverServicesWithCompletion:(PeripheralInitializationCompletionBlock)completion;
-(void)sendMessage:(const uint8_t*)dataIn dataSize:(uint32_t)dataLen;
@end


@interface BLEClient () <CBCentralManagerDelegate>
@property (strong, nonatomic) CBCentralManager *centralManager;
@property (strong, nonatomic) NSArray<CBUUID*>* servicesToScanFor;
@property (strong, nonatomic) NSMutableDictionary<NSUUID*, OICPeripheral*>* foundPeripherals;
@property (strong, nonatomic) NSMutableArray<CBPeripheral*>* peripheralList;
@property (strong, nonatomic) dispatch_semaphore_t initLock;
@property (strong, nonatomic) NSMutableArray<CBUUID*>* uuidList;

@end



@implementation BLEClient

-(id)init {
    self = [super init];
    if(self) {
        g_rxCharacteristicUUID = [CBUUID UUIDWithString:@CA_GATT_RESPONSE_CHRC_UUID];
        g_txCharacteristicUUID = [CBUUID UUIDWithString:@CA_GATT_REQUEST_CHRC_UUID];
        g_OICGattServiceUUID = [CBUUID UUIDWithString:@CA_GATT_SERVICE_UUID];

        _servicesToScanFor = @[g_OICGattServiceUUID];

        _uuidList = [[NSMutableArray alloc] init];
        [_uuidList addObject:g_OICGattServiceUUID];
        [_uuidList addObject:g_rxCharacteristicUUID];
        [_uuidList addObject:g_txCharacteristicUUID];

        _foundPeripherals = [[NSMutableDictionary alloc] init];
        _peripheralList = [[NSMutableArray alloc] init];

        _centralManager = [[CBCentralManager alloc] initWithDelegate:self queue:nil];

        //now wait until "centralManagerDidUpdateState" is called before returning
        //TODO: don't wait FOREVER!
        _initLock = dispatch_semaphore_create(0);
        dispatch_semaphore_wait(_initLock, DISPATCH_TIME_FOREVER);
    }
    return self;
}

-(void)startScanning {
    OIC_LOG_V(INFO, TAG, "%s", __FUNCTION__);

    //NOTE: Setting this option allows devices to disconnect and reconnect
    //      (otherwise, iOS/OSX filters out the advertisements and we never get notified)
    //      The caveat is that it may impact performance because of the additional callbacks.
    NSDictionary* options = @{ CBCentralManagerScanOptionAllowDuplicatesKey: @YES };
    [_centralManager scanForPeripheralsWithServices:_servicesToScanFor options:options];
}

-(void)stopScanning {
    OIC_LOG_V(INFO, TAG, "%s", __FUNCTION__);
    [_centralManager stopScan];
}

-(void)updateCharacteristicsTo:(const char*)remoteAddress withData:(const uint8_t*)data withLength:(uint32_t)dataLength {
    //find OICPeripheral that has this address

    //create a uuid as the key to lookup the device
    NSUUID* uuid = [[NSUUID alloc] initWithUUIDString:[[NSString alloc] initWithUTF8String:remoteAddress]];

    OICPeripheral* p = _foundPeripherals[uuid];
    if(p == nil) {
        OIC_LOG_V(WARNING, TAG, "%s: failed to find device with address=%s", __FUNCTION__, remoteAddress);
    } else {
        OIC_LOG_V(INFO, TAG, "%s: sending data to %s", __FUNCTION__, remoteAddress);
        [p sendMessage:data dataSize:dataLength];
    }
}

-(void)updateCharacteristicsToAll:(const uint8_t*)data withLength:(uint32_t)dataLength {
    OIC_LOG_V(INFO, TAG, "%s", __FUNCTION__);

    for(NSUUID* uuid in _foundPeripherals) {
        OICPeripheral* p = _foundPeripherals[uuid];
        [p sendMessage:data dataSize:dataLength];
    }
}

-(uint16_t)mtuFor:(const char*)remoteAddress {
    //find OICPeripheral that has this address
    uint16_t mtu;

    mtu = 20;
    NSUUID* uuid = [[NSUUID alloc] initWithUUIDString:[[NSString alloc] initWithUTF8String:remoteAddress]];

    OICPeripheral* p = _foundPeripherals[uuid];
    if (p == nil) {
        OIC_LOG_V(WARNING, TAG, "asking for MTU of nonexisting partner %s",
          remoteAddress);
    } else {
       mtu = [p.peripheral maximumWriteValueLengthForType:CBCharacteristicWriteWithoutResponse];
    }
    OIC_LOG_V(WARNING, TAG, "using MTU %d", mtu);
    return mtu;
}

-(void)removePeripheral:(CBPeripheral*)peripheral {
    if(peripheral == nil) {
        return;
    }
    [_foundPeripherals removeObjectForKey:peripheral.identifier];

    [_peripheralList removeObject:peripheral];

    //tell IoTivity that the device is gone
    if(_connectionStateChangedCallback) {
        const char* address = [[peripheral.identifier UUIDString] UTF8String];
        OIC_LOG_V(INFO, TAG, "%s: notifying IoTivity that device is gone: %s", __FUNCTION__, address);
        _connectionStateChangedCallback(CA_ADAPTER_GATT_BTLE, address, false);
    }
}


#pragma mark - CBCentralManagerDelegate Implementation

- (void)centralManagerDidUpdateState:(CBCentralManager *)central {
    OIC_LOG_V(INFO, TAG, "%s", __FUNCTION__);

    //notify init() function to continue
    dispatch_semaphore_signal(_initLock);
}

- (void)centralManager:(CBCentralManager *)central didDiscoverPeripheral:(CBPeripheral *)peripheral advertisementData:(NSDictionary<NSString *, id> *)advertisementData RSSI:(NSNumber *)RSSI {
    NSString* deviceName = [advertisementData objectForKey:CBAdvertisementDataLocalNameKey];

    //ignore it if we're already connected to it or we don't care about this advertisement packet
    if (_foundPeripherals[peripheral.identifier] != nil) {
        /* OIC_LOG_V(DEBUG, TAG, "%s: ignoring %s, %s", __FUNCTION__,
                     [deviceName UTF8String],
                 [[peripheral.identifier UUIDString] UTF8String]); */
        return;
    }

    //optionally, developers can use this to restrict connection to their device
    // if(![deviceName isEqualToString:@"OIC-DEVICE"]) {
    //     return;
    // }

    OIC_LOG_V(INFO, TAG, "%s: name=%s, (peripheral.name=%s), identifier=%s", __FUNCTION__, [deviceName UTF8String], [peripheral.name UTF8String], [[peripheral.identifier UUIDString] UTF8String]);

    //TODO: remove autoconnection logic (move to ?)

    OIC_LOG_V(INFO, TAG, "%s: trying to connect to peripheral %s", __FUNCTION__, [deviceName UTF8String]);

    OICPeripheral* p = [[OICPeripheral alloc] initWithPeripheral:peripheral];
    _foundPeripherals[p.peripheral.identifier] = p;

    //NOTE: we must keep a reference to the peripheral object otherwise it will disappear and the connect will fail silently
    [_peripheralList addObject:peripheral];

    //configure handler for when data is received
    [p setDataReceivedCallback:_dataReceivedCallback];

    //initiate connection
    [_centralManager connectPeripheral:peripheral options:nil];
}

- (void)centralManager:(CBCentralManager *)central didConnectPeripheral:(CBPeripheral *)peripheral {
    OICPeripheral* p = _foundPeripherals[peripheral.identifier];
    if(p == nil) {
        OIC_LOG_V(ERROR, TAG, "%s: failed finding OICPeripheral instance for %s", __FUNCTION__, [[peripheral.identifier UUIDString] UTF8String]);
    } else {
        const char* address = [p.address UTF8String];
        OIC_LOG_V(INFO, TAG, "%s: address=%s", __FUNCTION__, address);

        [peripheral discoverServices:@[g_OICGattServiceUUID]];

        if(_connectionStateChangedCallback) {
            OIC_LOG_V(INFO, TAG, "%s: calling connectionStateChangedCallback, address=[%s]", __FUNCTION__, address);
            _connectionStateChangedCallback(CA_ADAPTER_GATT_BTLE, address, true);
        }
    }
}

- (void)centralManager:(CBCentralManager *)central didDisconnectPeripheral:(CBPeripheral *)peripheral error:(NSError *)error {
    OIC_LOG_V(INFO, TAG, "%s: identifier=%s", __FUNCTION__, [[peripheral.identifier UUIDString] UTF8String]);
    if(error) {
        OIC_LOG_V(INFO, TAG, "%s", [[error description] UTF8String]);
    }

    //remove from list, tell IoTivity
    [self removePeripheral:peripheral];
}

- (void)centralManager:(CBCentralManager *)central didFailToConnectPeripheral:(CBPeripheral *)peripheral error:(NSError *)error {
    if(error) {
        OIC_LOG_V(ERROR, TAG, "%s - failed, identifier=%s", __FUNCTION__, [[peripheral.identifier UUIDString] UTF8String]);
        NSLog(@"error: %@", error);
    } else {
        OIC_LOG_V(INFO, TAG, "%s: identifier=%s", __FUNCTION__, [[peripheral.identifier UUIDString] UTF8String]);
    }

    //remove from list, tell IoTivity
    [self removePeripheral:peripheral];
}



@end // implementation for BLEClient



@implementation OICPeripheral

-(id)initWithPeripheral:(CBPeripheral *)peripheral {
    if (self = [super init]) {
        _peripheral = peripheral;
        _peripheral.delegate = self;

        _rxCharacteristic = nil;
        _txCharacteristic = nil;

        //make a copy of peripheral's address
        _address = [[NSString alloc] initWithString:[_peripheral.identifier UUIDString]];

        _state = OICDeviceStateDisconnected;
    }
    return self;
}

-(void)discoverServicesWithCompletion:(PeripheralInitializationCompletionBlock)completion {
    _initializationCompletionBlock = completion;
    if(_peripheral == nil) {
        OIC_LOG_V(WARNING, TAG, "%s: _peripheral is nil!", __FUNCTION__);
    } else {
        OIC_LOG_V(DEBUG, TAG, "%s", __FUNCTION__);

        [_peripheral discoverServices:@[ g_OICGattServiceUUID ]];
    }
}

-(void)sendMessage:(const uint8_t*)dataIn dataSize:(uint32_t)dataLen {
    OIC_LOG_V(DEBUG, TAG, "%s: peripheral (%s) <-------- writeValue  --------  central (%u bytes)", __FUNCTION__, [_address UTF8String], dataLen);
    if(_peripheral == nil) {
        OIC_LOG_V(ERROR, TAG, "%s: _peripheral is nil!", __FUNCTION__);
    }
    else if(_txCharacteristic == nil) {
        OIC_LOG_V(ERROR, TAG, "%s: _txCharacteristic is nil!", __FUNCTION__);
    }
    else if (_state == OICDeviceStateReady) {
        NSData* data = [NSData dataWithBytesNoCopy:(void*)dataIn length:dataLen freeWhenDone:NO];
        //OIC_LOG_V(DEBUG, TAG, "%s: Message %@ sent to BLE device", __FUNCTION__, data);
        [_peripheral writeValue:data forCharacteristic:_txCharacteristic type:CBCharacteristicWriteWithoutResponse];
    }
    else {
        OIC_LOG_V(ERROR, TAG, "%s: Error, trying to send a message before the device is ready. state=%d", __FUNCTION__, (int)_state);
    }
}

#pragma mark - CBPeripheralDelegate Implementation

- (void)peripheral:(CBPeripheral *)peripheral didDiscoverServices:(nullable NSError *)error {
    if (error) {
        NSLog(@"%@", error);
        return;
    }

    if ([peripheral.services count] < 1) {
        /* NSLog(@"peripheral %s %s does not have OIC service",
             [peripheral.name UTF8String], [[peripheral.identifier UUIDString] UTF8String]); */
        return;
    }
    _state = OICDeviceStateConnected;

    [peripheral discoverCharacteristics:@[g_rxCharacteristicUUID, g_txCharacteristicUUID] forService:peripheral.services[0]];
}

- (void)peripheral:(CBPeripheral *)peripheral didDiscoverCharacteristicsForService:(CBService *)service error:(nullable NSError *)error {
    OIC_LOG_V(INFO, TAG, "%s", __FUNCTION__);

    for (CBCharacteristic *characteristic in service.characteristics) {
        if ([characteristic.UUID isEqual:g_rxCharacteristicUUID]) {
            _rxCharacteristic = characteristic;
            [peripheral setNotifyValue:YES forCharacteristic:characteristic];
        }
        if ([characteristic.UUID isEqual:g_txCharacteristicUUID]) {
            _txCharacteristic = characteristic;
        }
    }

    if ( _rxCharacteristic && _txCharacteristic ) {
        _state = OICDeviceStateReady;
        if (_initializationCompletionBlock != nil) {
            _initializationCompletionBlock();
            _initializationCompletionBlock = nil;
        }
    }
    else {
        OIC_LOG_V(ERROR, TAG, "%s - failed finding rx and tx characteristics", __FUNCTION__);
    }
}

- (void)peripheral:(CBPeripheral *)peripheral didWriteValueForCharacteristic:(CBCharacteristic *)characteristic error:(nullable NSError *)error {
    OIC_LOG_V(INFO, TAG, "%s", __FUNCTION__);
}

- (void)peripheral:(CBPeripheral *)peripheral didUpdateValueForCharacteristic:(CBCharacteristic *)characteristic error:(nullable NSError *)error {
    if([characteristic.UUID isEqual:g_rxCharacteristicUUID]) {
        NSLog(@"peripheral (%s) ------- characteristics update --------> central", [_address UTF8String]);

        if(_dataReceivedCallback != NULL) {
            NSData* data = characteristic.value;
            uint32_t bytesSent = 0;

            _dataReceivedCallback([_address UTF8String], (const uint8_t *)[data bytes], (uint32_t)[data length], &bytesSent);
        } else {
            OIC_LOG_V(WARNING, TAG, "%s: dataReceivedCallback is null!", __FUNCTION__);
        }
    }
}


@end // implementation for OICPeripheral


#include "BLEClient.h"

#include <cagattservice.h>  //contains GUIDs defined by IoTivity
#include <logger.h>

#define TAG "BLEClient"

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
@property (strong, nonatomic) NSMutableDictionary<CBUUID*, CBService*>* services;
@property (strong, nonatomic) NSMutableDictionary<CBUUID*, CBCharacteristic*>* reqChars;
@property (strong, nonatomic) NSMutableDictionary<CBUUID*, CBCharacteristic*>* rspChars;
@property (strong, atomic) dispatch_semaphore_t discoverLock;
-(id)initWithPeripheral:(CBPeripheral*)peripheral;
//-(void)discoverServicesWithCompletion:(PeripheralInitializationCompletionBlock)completion;
-(void)sendMessage:(const uint8_t*)dataIn dataSize:(uint32_t)dataLen;
@end


@interface BLEClient () <CBCentralManagerDelegate>
@property (strong, nonatomic) CBCentralManager *centralManager;
@property (strong, nonatomic) NSArray<CBUUID*>* servicesToScanFor;
@property (strong, nonatomic) NSMutableDictionary<NSUUID*, OICPeripheral*>* foundPeripherals;
@property (strong, nonatomic) NSMutableArray<CBPeripheral*>* peripheralList;
@property (strong, nonatomic) dispatch_semaphore_t initLock;

@end



@implementation BLEClient

-(id)init {
    self = [super init];
    if(self) {

        _servicesToScanFor = @[[CBUUID UUIDWithString:@CA_DEFAULT_GATT_SERVICE_UUID]];

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
    //create a uuid as the key to lookup the device
    NSUUID* uuid = [[NSUUID alloc] initWithUUIDString:[[NSString alloc] initWithUTF8String:remoteAddress]];

    OICPeripheral* p = _foundPeripherals[uuid];
    if(p == nil) {
        OIC_LOG_V(WARNING, TAG, "%s: failed to find device with address=%s", __FUNCTION__, remoteAddress);
    } else {
        OIC_LOG_V(DEBUG, TAG, "Peripheral state = %lu", (unsigned long)p.state);
        // If the peripheral is already in a ready state, no need to reconnect
        if (p.state != OICDeviceStateReady) {
            OIC_LOG_V(INFO, TAG, "%s: connecting to device %s", __FUNCTION__, remoteAddress);
            [_centralManager connectPeripheral:p.peripheral options:nil];
        }

        // TODO this is hacky and should be replaced with something better
        // Wait until the OICPeripheral is in a ready state before sending the message
        int j = 0;
        while(p.state != OICDeviceStateReady && j != 10) {
            [NSThread sleepForTimeInterval:0.5f];
            j = j+1;
        }   
        if (p.state != OICDeviceStateReady) {
//            [_centralManager cancelPeripheralConnection:p.peripheral];
            OIC_LOG_V(ERROR, TAG, "%s: OICPeripheral is not in a ready state. BLEClient can not send the message.", __FUNCTION__);
            return;
        }   

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
       mtu = [p.peripheral maximumWriteValueLengthForType:CBCharacteristicWriteWithResponse];
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
	OIC_LOG_V(INFO, TAG, "%s: adding peripheral %s to foundPeripherals and peripheralList", __FUNCTION__, [[peripheral.identifier UUIDString] UTF8String]);

    OICPeripheral* p = [[OICPeripheral alloc] initWithPeripheral:peripheral];
    _foundPeripherals[p.peripheral.identifier] = p;

    //NOTE: we must keep a reference to the peripheral object otherwise it will disappear and the connect will fail silently
    [_peripheralList addObject:peripheral];

    //configure handler for when data is received
    [p setDataReceivedCallback:_dataReceivedCallback];
}

- (void)centralManager:(CBCentralManager *)central didConnectPeripheral:(CBPeripheral *)peripheral {
    OICPeripheral* p = _foundPeripherals[peripheral.identifier];
    if(p == nil) {
        OIC_LOG_V(ERROR, TAG, "%s: failed finding OICPeripheral instance for %s", __FUNCTION__, [[peripheral.identifier UUIDString] UTF8String]);
    } else {
        const char* address = [p.address UTF8String];
        OIC_LOG_V(INFO, TAG, "%s: address=%s", __FUNCTION__, address);
        NSString* uuidString = [NSString stringWithUTF8String:g_gattServiceUUID];
        NSLog(@"UUID STRING: %@", uuidString);
        CBUUID* uuid = [CBUUID UUIDWithString:uuidString];

        [peripheral discoverServices:@[uuid]];

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
        
        _discoverLock = nil;
        
        // Allocate and initialize the services and request and reponse characteristic dictionaries
        _services = [[NSMutableDictionary<CBUUID*, CBService*> alloc] init];
        _reqChars = [[NSMutableDictionary<CBUUID*, CBCharacteristic*> alloc] init];
        _rspChars = [[NSMutableDictionary<CBUUID*, CBCharacteristic*> alloc] init];

        //make a copy of peripheral's address
        _address = [[NSString alloc] initWithString:[_peripheral.identifier UUIDString]];

        _state = OICDeviceStateDisconnected;
    }
    return self;
}

-(void)sendMessage:(const uint8_t*)dataIn dataSize:(uint32_t)dataLen {
    OIC_LOG_V(DEBUG, TAG, "%s: peripheral (%s) <-------- writeValue  --------  central (%u bytes)", __FUNCTION__, [_address UTF8String], dataLen);
    
    // Check if peripheral is nil
    if (_peripheral == nil) {
        OIC_LOG_V(ERROR, TAG, "%s: _peripheral is nil!", __FUNCTION__);
        return;
    }
    
    // Get target characteristic UUIDs
    CBUUID* targetServiceUUID = [CBUUID UUIDWithString:[NSString stringWithUTF8String:g_gattServiceUUID]];
    CBUUID* targetReqCharUUID = [CBUUID UUIDWithString:[NSString stringWithUTF8String:g_gattRequestCharacteristicUUID]];
    CBUUID* targetRspCharUUID = [CBUUID UUIDWithString:[NSString stringWithUTF8String:g_gattResponseCharacteristicUUID]];
    
    // Check if we have the service for the targetUUID
    if ([_services objectForKey:targetServiceUUID] == nil) {
        OIC_LOG_V(INFO, TAG, "%s: The target service UUID was not found in the services dictionary, discovering services...", __FUNCTION__);
        // Initialize discoverLock
        _discoverLock = dispatch_semaphore_create(0);
        // Discover services and characteristics for the target UUIDs
        [_peripheral discoverServices:@[targetServiceUUID]];
        dispatch_semaphore_wait(_discoverLock, DISPATCH_TIME_FOREVER);
        OIC_LOG_V(DEBUG, TAG, "wait has completed");
    }
    
    // Get the request characteristic from the dictionary using the target UUID
    CBCharacteristic* reqChar = [_reqChars objectForKey:targetReqCharUUID];
    
    // Check if we have the characteristic for the target UUID
    if (reqChar == nil) {
        OIC_LOG_V(ERROR, TAG, "%s: The target request characteristic UUID was not found in the reqeust characteristics dictionary", __FUNCTION__);
        return;
    }
    
    // Check if the peripheral is in a ready state and write the value
    if (_state == OICDeviceStateReady) {
        NSData* data = [NSData dataWithBytesNoCopy:(void*)dataIn length:dataLen freeWhenDone:NO];
        [_peripheral writeValue:data forCharacteristic:reqChar type:CBCharacteristicWriteWithResponse];
    } else {
        OIC_LOG_V(ERROR, TAG, "%s: Error, trying to send a message before the device is ready. state=%d", __FUNCTION__, (int)_state);
    }
}

#pragma mark - CBPeripheralDelegate Implementation

- (void)peripheral:(CBPeripheral *)peripheral didDiscoverServices:(nullable NSError *)error {
    OIC_LOG_V(DEBUG, TAG, "didDiscoverServices: peripheral=%s, with %lu services:", [[peripheral.identifier UUIDString] UTF8String], (unsigned long)peripheral.services.count);
    
    for (CBService* service in peripheral.services) {
        OIC_LOG_V(DEBUG, TAG, "\t%s", [service.UUID.UUIDString UTF8String]);
    }
    
    if (error) {
        NSLog(@"%@", error);
        return;
    }

    if ([peripheral.services count] < 1) {
        return;
    }
    
    _state = OICDeviceStateConnected;
    
    CBUUID* targetServiceUUID = [CBUUID UUIDWithString:[NSString stringWithUTF8String:g_gattServiceUUID]];
    CBUUID* reqCharUUID = [CBUUID UUIDWithString:[NSString stringWithUTF8String:g_gattRequestCharacteristicUUID]];
    CBUUID* rspCharUUID = [CBUUID UUIDWithString:[NSString stringWithUTF8String:g_gattResponseCharacteristicUUID]];
    
    for (CBService* service in peripheral.services) {
        if ([service.UUID isEqual:targetServiceUUID]) {
            [_services setObject:service forKey:service.UUID];
            NSLog(@"added service (%@) to OICPeripheral's services dictionary: %@", service.UUID.UUIDString, _services);
            [peripheral discoverCharacteristics:@[reqCharUUID, rspCharUUID] forService:service];
            break;
        }
    }
    
}

- (void)peripheral:(CBPeripheral *)peripheral didDiscoverCharacteristicsForService:(CBService *)service error:(nullable NSError *)error {
    OIC_LOG_V(INFO, TAG, "%s", __FUNCTION__);

    CBUUID* targetReqCharUUID = [CBUUID UUIDWithString:[NSString stringWithUTF8String:g_gattRequestCharacteristicUUID]];
    CBUUID* targetRspCharUUID = [CBUUID UUIDWithString:[NSString stringWithUTF8String:g_gattResponseCharacteristicUUID]];
    bool reqCharFound = false;
    bool rspCharFound = false;
    
    for (CBCharacteristic *characteristic in service.characteristics) {
        OIC_LOG_V(DEBUG, TAG, "\t%s", [characteristic.UUID.UUIDString UTF8String]);
        
        if ([characteristic.UUID isEqual:targetReqCharUUID]) {
            [_reqChars setObject:characteristic forKey:characteristic.UUID];
            NSLog(@"added request characteristic (%@) to OICPeripheral's reqChars dictionary: %@", characteristic.UUID.UUIDString, _reqChars);
            reqCharFound = true;
        }
        if ([characteristic.UUID isEqual:targetRspCharUUID]) {
            [_rspChars setObject:characteristic forKey:characteristic.UUID];
            [peripheral setNotifyValue:YES forCharacteristic:characteristic];
            NSLog(@"added response characteristic (%@) to OICPeripheral's reqChars dictionary: %@", characteristic.UUID.UUIDString, _rspChars);
            rspCharFound = true;
        }
    }
    OIC_LOG_V(DEBUG, TAG, "reqCharFound=%s, rspCharFound=%s", reqCharFound?"true":"false", rspCharFound?"true":"false");
    if (reqCharFound && rspCharFound) {
        _state = OICDeviceStateReady;
        if (_discoverLock != nil) {
            OIC_LOG_V(DEBUG, TAG, "Signaling discover lock");
            dispatch_semaphore_signal(_discoverLock);
            dispatch_release(_discoverLock);
            _discoverLock = nil;
        }
    }
    else {
        OIC_LOG_V(ERROR, TAG, "%s - failed finding request and response characteristics", __FUNCTION__);
    }
}

- (void)peripheral:(CBPeripheral *)peripheral didWriteValueForCharacteristic:(CBCharacteristic *)characteristic error:(nullable NSError *)error {
    OIC_LOG_V(INFO, TAG, "%s%@", __FUNCTION__, error);
}

- (void)peripheral:(CBPeripheral *)peripheral didUpdateValueForCharacteristic:(CBCharacteristic *)characteristic error:(nullable NSError *)error {
    CBUUID* targetRspCharUUID = [CBUUID UUIDWithString:[NSString stringWithUTF8String:g_gattResponseCharacteristicUUID]];
    OIC_LOG_V(DEBUG, TAG, "peripheral (%s) ------- characteristics update --------> central", [_address UTF8String]);
    if([characteristic.UUID isEqual:targetRspCharUUID]) {
        if(_dataReceivedCallback != NULL) {
            NSData* data = characteristic.value;
            uint32_t bytesSent = 0;

            _dataReceivedCallback([_address UTF8String], (const uint8_t *)[data bytes], (uint32_t)[data length], &bytesSent);
        } else {
            OIC_LOG_V(WARNING, TAG, "%s: dataReceivedCallback is null!", __FUNCTION__);
        }
    } else {
        OIC_LOG_V(ERROR, TAG, "UUID of peripheral response characteristic does not match Iotivity's response characteristic.");
        OIC_LOG_V(ERROR, TAG, "\tperipheral: %s", [characteristic.UUID.UUIDString UTF8String]);
        OIC_LOG_V(ERROR, TAG, "\tiotivity: %s", g_gattResponseCharacteristicUUID);
    }
}


@end // implementation for OICPeripheral


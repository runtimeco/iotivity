#pragma once

#include <CoreBluetooth/CoreBluetooth.h>

#include <caleinterface.h>


@interface BLEServer : NSObject<CBPeripheralManagerDelegate>

@property CABLEDataReceivedCallback dataReceivedCallback;

@property CABLEErrorHandleCallback errorHandleCallback;

@property CALEConnectionStateChangedCallback connectionStateChangedCallback;

-(id)init;
-(void)startAdvertising;
-(void)stopAdvertising;

-(void)updateCharacteristicsTo:(const char*)address withData:(const uint8_t*)data withLength:(uint32_t)dataLength;
-(void)updateCharacteristicsToAll:(const uint8_t*)data withLength:(uint32_t)dataLength;

@end

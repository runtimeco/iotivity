#pragma once

#include <CoreBluetooth/CoreBluetooth.h>

#include <caleinterface.h>


@interface BLEClient : NSObject<CBCentralManagerDelegate>

@property CABLEDataReceivedCallback dataReceivedCallback;

@property CABLEErrorHandleCallback errorHandleCallback;

@property CALEConnectionStateChangedCallback connectionStateChangedCallback;

-(id)init;
-(void)startScanning;
-(void)stopScanning;

-(void)updateCharacteristicsTo:(const char*)remoteAddress withData:(const uint8_t*)data withLength:(uint32_t)dataLen;

-(void)updateCharacteristicsToAll:(const uint8_t*)data withLength:(uint32_t)dataLength;

-(uint16_t)mtuFor:(const char*)remoteAddress;
-(void)setTargetService:(const char*)uuid;
@end

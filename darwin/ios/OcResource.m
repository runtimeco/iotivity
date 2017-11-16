
#import "OcResource.h"

@implementation OcResource

- (instancetype) init:(OCClientResponse *) response :(OCResourcePayload *) resource {
    
    // Set the ocDevAddr for use in subsequent calls on this resource
    _ocDevAddr = response->devAddr;
    
    // Resource address and URI
    _address = [NSString stringWithUTF8String:response->devAddr.addr];
    _uri = [NSString stringWithUTF8String:resource->uri];
    
    // Allocate resource type array and add values
    _resourceTypes = [[NSMutableArray alloc] init];
    for (OCStringLL * type = resource->types; type; type = type->next) {
        [_resourceTypes addObject:[NSString stringWithUTF8String:type->value]];
    }
    // Allocate resource interfaces array and add values
    _resourceInterfaces = [[NSMutableArray alloc] init];
    for (OCStringLL * iface = resource->interfaces; iface; iface = iface->next) {
        [_resourceInterfaces addObject:[NSString stringWithUTF8String:iface->value]];
    }

    // Connectivity adapter type and connectivity type
    _adapterType = response->devAddr.adapter;
    _connectivityType = response->connType;

    return self;
}

// GET
- (int) get:(GetCallback)callback {
    return [[IotivityClient shared] getResource:self qos:OC_LOW_QOS callback:callback];
}
- (int) get:(OCQualityOfService)qos callback:(GetCallback)callback {
    return [[IotivityClient shared] getResource:self qos:qos callback:callback];
}

// PUT
- (int) put:(NSArray<OcRepresentationValue*>*)values callback:(PutCallback)callback {
    return [[IotivityClient shared] putResource:self values:values qos:OC_LOW_QOS callback:callback];
}
- (int) put:(NSArray<OcRepresentationValue*>*)values qos:(OCQualityOfService)qos callback:(PutCallback)callback {
    return [[IotivityClient shared] putResource:self values:values qos:qos callback:callback];
}

// OBSERVE
- (int) observe:(ObserveCallback)callback {
    return [[IotivityClient shared] observeResource:self qos:OC_LOW_QOS callback:callback];
}
- (int) observe:(OCQualityOfService)qos callback:(ObserveCallback)callback {
    return [[IotivityClient shared] observeResource:self qos:qos callback:callback];
}

// CANCEL OBSERVE
- (int) cancelObserve {
    return [[IotivityClient shared] cancelObserve:self];
}

- (NSString *)description {
    return [NSString stringWithFormat: @"Resource: %@\r\tAddress: %@\r\tResource Types: %@\r\tResource Interfaces: %@\r\tAdapter Type: %u\r\tConnectivityType: %x\r", _uri, _address, _resourceTypes, _resourceInterfaces, _adapterType, _connectivityType];
}
@end


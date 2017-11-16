
#import "IotivityClient.h"

// TODO Add support for: interface/resourceType queries, value queries etc.
// TODO Add support for: coap header options
// TODO Add support for: POST 
// TODO Get method descriptions into framework 

@interface IotivityClient ()

// Dictionaries of closure callbacks
@property (atomic, readonly) NSMutableDictionary<NSData*, DiscoverCallback>* discoverCallbacks;
@property (atomic, readonly) NSMutableDictionary<NSData*, GetCallback>* getCallbacks;
@property (atomic, readonly) NSMutableDictionary<NSData*, PutCallback>* putCallbacks;
@property (atomic, readonly) NSMutableDictionary<NSData*, ObserveCallback>* observeCallbacks;
@property (atomic, readonly) NSMutableDictionary<NSString*, NSValue*>* observeHandles;

// Default QoS and Connectivity Type
@property (atomic) OCConnectivityType defaultConnType;  // CT_ADAPTER_GATT_BTLE;
@property (atomic) OCQualityOfService defaultQos;       // OC_LOW_QOS;

@end

@implementation IotivityClient

+ (instancetype)shared
{
    static IotivityClient *iot;
    
    if (!iot) {
        iot = [[IotivityClient alloc] initPrivate];
    }
    return iot;
}

- (instancetype)init
{
    @throw [NSException exceptionWithName:@"Singleton"
                                   reason:@"use +[IotivityClient shared]"
                                   userInfo: nil];
    return nil;
}

- (instancetype)initPrivate
{
    self = [super init];
    if (self) {
        // Initialize callback dictionaries
        _discoverCallbacks = [[NSMutableDictionary alloc] init];
        _getCallbacks = [[NSMutableDictionary alloc] init];
        _putCallbacks = [[NSMutableDictionary alloc] init];
        _observeCallbacks = [[NSMutableDictionary alloc] init];
        _observeHandles = [[NSMutableDictionary alloc] init];
        
        // Initialize defaults
        _defaultConnType = CT_ADAPTER_GATT_BTLE;
        _defaultQos = OC_LOW_QOS;
        
        [self performSelectorInBackground:@selector(doWork) withObject:nil];
    }
    return self;
}

- (instancetype)doWork
{
    OCStackResult rc;

    rc = OCInit(NULL, 0, OC_CLIENT);
    if (rc != 0) {
        NSLog(@"OCInit failed: %d\n", rc);
        return NULL;
    }
    while (1) {
        OCProcess();
        [NSThread sleepForTimeInterval:0.01];
    }
}

- (int) setTargetUUIDs:(NSString*)serviceUUID requestUUID:(NSString*)requestUUID responseUUID:(NSString*)responseUUID {
    CAResult_t result = CALESetServiceUUID([serviceUUID UTF8String]);
    if (result == CA_STATUS_FAILED) {
        NSLog(@"set service uuid failed");
        return result;
    }
    result = CALESetRequestCharacteristicUUID([requestUUID UTF8String]);
    if (result == CA_STATUS_FAILED) {
        NSLog(@"set request uuid failed");
        return result;
    }
    result = CALESetResponseCharacteristicUUID([responseUUID UTF8String]);
    if (result == CA_STATUS_FAILED) {
        NSLog(@"set response uuid failed");
        return result;
    }
    return result;
}


///////////////////////////////////////////////////////////////////////////////
//  DISCOVERY
///////////////////////////////////////////////////////////////////////////////

/**
 * @discussion This method will perform multicast resource discovery over the
 transport specified in the given connectivity type.
 * @param connType The transport to perform multicast resource discovery over.
 This value should include one adapter enum flag (CT_ADAPTER_XXX).
 * @param callback the callback to be called when discovery has completed.
 * @return the OCStackResult status code returned by OCDoResource.
 */
- (int) discoverMulticast:(OCConnectivityType)connType
                 callback:(DiscoverCallback)callback
{
    return [self discoverMulticast:connType qos:OC_LOW_QOS callback:callback];
}

/**
 * @discussion This method will perform multicast resource discovery over the
 transport specified in the given connectivity type.
 * @param connType The transport to perform multicast resource discovery over.
 This value should include one adapter enum flag (CT_ADAPTER_XXX).
 * @param qos The quality of service to use for this transaction.
 * @param callback the callback to be called when discovery has completed.
 * @return the OCStackResult status code returned by OCDoResource.
 */
- (int) discoverMulticast:(OCConnectivityType)connType
                      qos:(OCQualityOfService)qos
                 callback:(DiscoverCallback)callback
{
    // Create a random handle to use to identify the callback to use
    void *handle = malloc(CA_MAX_TOKEN_LEN);
    arc4random_buf(handle, CA_MAX_TOKEN_LEN);
    NSData *dataHandle = [NSData dataWithBytes:handle length:CA_MAX_TOKEN_LEN];
    // Add callback to dictionary
    [_discoverCallbacks setObject:callback forKey:dataHandle];
    
    // Set callback
    OCCallbackData cb = {
        .cb = discover_cb
    };
    return OCDoResource(handle, OC_REST_DISCOVER, OC_RSRVD_WELL_KNOWN_URI, NULL,
                        NULL, connType, qos, &cb, NULL, 0);
}

/**
 * @discussion This method will perform unicast resource discovery over the
 transport specified in the the adapter parameter to the device given by the
 address.
 * @param address The address of the device to perform resource discovery on
 * @param connType The transport to perform unicast resource discovery over.
 This value should include one adapter enum flag (CT_ADAPTER_XXX).
 * @param qos The quality of service to use for this transaction.
 * @param callback The callback called when the transaction has finished.
 * @return OCStackResult status code returned by OCDoResource
 */
- (int) discoverUnicast:(NSString*)address
               connType:(OCConnectivityType)connType
                    qos:(OCQualityOfService)qos
               callback:(DiscoverCallback)callback
{
    NSLog(@"Calling unicast DISCOVER on: %@", address);
    if ([address length] == 0) {
        NSLog(@"ERROR: input address is invalid");
        return OC_STACK_ERROR;
    }
    
    // Create a random handle to use to identify the callback to use
    void *handle = malloc(CA_MAX_TOKEN_LEN);
    arc4random_buf(handle, CA_MAX_TOKEN_LEN);
    NSData *dataHandle = [NSData dataWithBytes:handle length:CA_MAX_TOKEN_LEN];
    // Add callback to dictionary
    [_discoverCallbacks setObject:callback forKey:dataHandle];
    
    // Set callback
    OCCallbackData cb = {
        .cb = discover_cb
    };
    
    // Create dev addr from address
    OCDevAddr devAddr = {
        .adapter = connType >> CT_ADAPTER_SHIFT
    };
    strcpy(devAddr.addr,[address UTF8String]);
    
    return OCDoResource(handle, OC_REST_DISCOVER, OC_RSRVD_WELL_KNOWN_URI, &devAddr,
                        NULL, connType, OC_LOW_QOS, &cb, NULL, 0);
}

// Discovery Callback
static OCStackApplicationResult
discover_cb(void *ctx, OCDoHandle handle, OCClientResponse *rsp)
{
    // Get shared IotivityClient object
    IotivityClient *iot = [IotivityClient shared];
    
    // Make sure OCClientResponse is not nil
    if (!rsp) {
        NSLog(@"ERROR: OCClientResponse is nil!");
        return OC_STACK_DELETE_TRANSACTION;
    } else if (rsp->result == OC_STACK_ERROR) {
        NSLog(@"ERROR: DISCOVERY resulted in an error response. Keeping transaction for additional transports.");
        return OC_STACK_KEEP_TRANSACTION;
    }
    
    // Get address and uri from OCClientResponse object
    NSString* address = [NSString stringWithUTF8String:rsp->addr->addr];
    NSString* uri = [NSString stringWithUTF8String:rsp->resourceUri];
    NSLog(@"DISCOVER callback from: %@ %@", address, uri);
    
    // Create OCResources from discovery payload's resources
    OCDiscoveryPayload* discoveryPayload = (OCDiscoveryPayload*)rsp->payload;
    NSMutableArray<OcResource*>* resources = [[NSMutableArray alloc] init];
    if (discoveryPayload == NULL) {
        NSLog(@"ERROR: OCDiscoveryPayload is nil!");
    } else {
        // Loop through resources in the response's discovery payload and add 
        // each resource to the resource array.
        for (OCResourcePayload* resource = discoveryPayload->resources; resource; resource = resource->next) {
            OcResource* ocResource = [[OcResource alloc] init:rsp:resource];
            [resources addObject:ocResource];
        }
    }
    
    // Get callback from the discover callback dictionary
    NSData* data = [NSData dataWithBytes:handle length:CA_MAX_TOKEN_LEN];
    DiscoverCallback callback = [iot.discoverCallbacks objectForKey:data];
    if (callback != nil) {
        // If the callback exists, call the callback on the main thread
        dispatch_async(dispatch_get_main_queue(), ^{
            callback(resources);
            [iot.discoverCallbacks removeObjectForKey:data];
            [resources autorelease];
        });
    } else {
        NSLog(@"ERROR: Callback not found!");
    }
    
    return OC_STACK_DELETE_TRANSACTION;
}

///////////////////////////////////////////////////////////////////////////////
//  GET
///////////////////////////////////////////////////////////////////////////////

/**
 * @discussion This method gets the values from a CoAP resource using the
 default connectivity type and quality of service.
 * @param uri The URI from of the resource to GET
 * @param address The address of the device hosting the resource
 * @param callback The callback called when the transaction has finished.
 * @return OCStackResult status code returned by OCDoResource
 */
- (int) getResource:(NSString *)uri
            address:(NSString *)address
           callback:(GetCallback)callback
{
    // Create a devAddr and set the adapter and address
    OCDevAddr devAddr = {
        .adapter = _defaultConnType >> CT_ADAPTER_SHIFT
    };
    strcpy(devAddr.addr, [address UTF8String]);
    
    // Call getResource with devAddr
    return [self getResource:uri address:address connType:_defaultConnType qos:_defaultQos callback:callback];
}

/**
 * @discussion This method gets the values from a CoAP resource
 * @param uri The URI from of the resource to GET
 * @param address The address of the device hosting the resource
 * @param connType The connectivity type to perform this transaction over.
 This value should include one adapter enum flag (CT_ADAPTER_XXX).
 * @param qos The quality of service to use for this transaction.
 * @param callback The callback called when the transaction has finished.
 * @return OCStackResult status code returned by OCDoResource
 */
- (int) getResource:(NSString *)uri
            address:(NSString *)address
           connType:(OCConnectivityType)connType
                qos:(OCQualityOfService)qos
           callback:(GetCallback)callback
{
    // Create a devAddr and set the adapter and address
    OCDevAddr devAddr = {
        .adapter = connType >> CT_ADAPTER_SHIFT
    };
    strcpy(devAddr.addr,[address UTF8String]);
    
    // Call getResource with devAddr
    return [self getResource:uri devAddr:devAddr connType:connType qos:qos callback:callback];
}

/**
 * @discussion This method gets the values from a OCF resource
 * @param resource An OcResource object representing an OCF resource
 * @param qos The quality of service to use for this transaction.
 * @param callback The callback called when the transaction has finished.
 * @return OCStackResult status code returned by OCDoResource
 */
- (int) getResource:(OcResource*)resource
                qos:(OCQualityOfService)qos
           callback:(GetCallback)callback
{
    return [self getResource:resource.uri devAddr:resource.ocDevAddr connType:resource.connectivityType qos:qos callback:callback];
}

/**
 * @discussion This method gets the values from a resource
 * @param uri The URI of the resource to perform this transaction on
 * @param devAddr An OCDevAddr struct representing the target device
 * @param connType The connectivity type to perform this transaction over.
 This value should include one adapter enum flag (CT_ADAPTER_XXX).
 * @param qos The quality of service to use for this transaction.
 * @param callback The callback called when the transaction has finished.
 * @return OCStackResult status code returned by OCDoResource
 */
- (int) getResource:(NSString*)uri
            devAddr:(OCDevAddr)devAddr
           connType:(OCConnectivityType)connType
                qos:(OCQualityOfService)qos
           callback:(GetCallback)callback
{
    NSLog(@"Calling GET on: %s %@", devAddr.addr, uri);
    if ([uri length] == 0) {
        NSLog(@"ERROR: input uri is invalid");
        return OC_STACK_ERROR;
    } else if (devAddr.addr[0] == '\0') {
        NSLog(@"ERROR: input address is invalid");
        return OC_STACK_ERROR;
    }
    
    // Create a random handle to use to identify the callback to use
    void *handle = malloc(CA_MAX_TOKEN_LEN);
    arc4random_buf(handle, CA_MAX_TOKEN_LEN);
    NSData *dataHandle = [NSData dataWithBytes:handle length:CA_MAX_TOKEN_LEN];
    // Add callback to dictionary
    [_getCallbacks setObject:callback forKey:dataHandle];
    
    // Set callback
    OCCallbackData cb = {
        .cb = oc_get_cb
    };

    return OCDoResource(handle, OC_REST_GET, [uri UTF8String], &devAddr, NULL,
                        connType, qos, &cb, NULL, 0);
}

static OCStackApplicationResult
oc_get_cb(void *ctx, OCDoHandle handle, OCClientResponse *rsp)
{
    // Get shared IotivityClient object
    IotivityClient *iot = [IotivityClient shared];
    
    // Make sure OCClientResponse is not nil
    if (!rsp) {
        NSLog(@"ERROR: OCClientResponse is nil!");
        return OC_STACK_DELETE_TRANSACTION;
    }
    
    // Get address and uri from OCClientResponse object
    NSString* address = [NSString stringWithUTF8String:rsp->addr->addr];
    NSString* uri = [NSString stringWithUTF8String:rsp->resourceUri];
    NSLog(@"GET callback from: %@ %@", address, uri);
    
    // If the response payload is not nil, construct an OcRepresentation to be passed to callback
    OcRepresentation *ocRepresentation = nil;
    if (!rsp->payload) {
        NSLog(@"WARN: OCClientResponse->payload is nil!");
        ocRepresentation = [[OcRepresentation alloc] init:rsp:nil];
    } else if (rsp->payload->type == PAYLOAD_TYPE_REPRESENTATION) {
        OCRepPayload *representation_payload = (OCRepPayload *)rsp->payload;
        ocRepresentation = [[OcRepresentation alloc] init:rsp:representation_payload];
    }
    
    // Get callback from the get callback dictionary
    NSData* data = [NSData dataWithBytes:handle length:CA_MAX_TOKEN_LEN];
    GetCallback callback = [iot.getCallbacks objectForKey:data];
    if (callback != nil) {
        // If the callback exists, call the callback on the main thread
        dispatch_async(dispatch_get_main_queue(), ^{
            callback(ocRepresentation);
            [iot.getCallbacks removeObjectForKey:data];
            [ocRepresentation autorelease];
        });
    } else {
        NSLog(@"ERROR: Callback not found!");
    }
    
    return OC_STACK_DELETE_TRANSACTION;
}

///////////////////////////////////////////////////////////////////////////////
//  PUT
///////////////////////////////////////////////////////////////////////////////

/**
 * @discussion This method puts values to a CoAP resoruce using the default
 connectivity type and quality of service.
 * @param uri The URI of the resource to perform this transaction on
 * @param address The address of the device hosting the resource
 * @param values An array of values to PUT to the resource
 * @param callback The callback called when the transaction has finished.
 * @return OCStackResult status code returned by OCDoResource
 */
- (int) putResource:(NSString*)uri
            address:(NSString*)address
             values:(NSArray<OcRepresentationValue*>*)values
           callback:(PutCallback)callback
{
    return [self putResource:uri address:address values:values connType:_defaultConnType qos:_defaultQos callback:callback];
}

/**
 * @discussion This method puts values to a CoAP resoruce using the default
 connectivity type and quality of service.
 * @param uri The URI of the resource to perform this transaction on
 * @param address The address of the device hosting the resource
 * @param values An array of values to PUT to the resource
 * @param connType The connectivity type to perform this transaction over.
 This value should include one adapter enum flag (CT_ADAPTER_XXX).
 * @param qos The quality of service to use for this transaction.
 * @param callback The callback called when the transaction has finished.
 * @return OCStackResult status code returned by OCDoResource
 */
- (int) putResource:(NSString*)uri
            address:(NSString*)address
             values:(NSArray<OcRepresentationValue*>*)values
           connType:(OCConnectivityType)connType
                qos:(OCQualityOfService)qos
           callback:(PutCallback)callback
{
    // Create a devAddr and set the adapter and address
    OCDevAddr devAddr = {
        .adapter = connType >> CT_ADAPTER_SHIFT
    };
    strcpy(devAddr.addr,[address UTF8String]);
    
    return [self putResource:uri devAddr:devAddr values:values connType:connType qos:qos callback:callback];

}

/**
 * @discussion This method PUTs the values to an OCF resource
 * @param resource An OcResource object representing an OCF resource
 * @param qos The quality of service to use for this transaction.
 * @param callback The callback called when the transaction has finished.
 * @return OCStackResult status code returned by OCDoResource
 */
- (int) putResource:(OcResource*)resource
             values:(NSArray<OcRepresentationValue*>*)values
                qos:(OCQualityOfService)qos
           callback:(PutCallback)callback
{
    return [self putResource:resource.uri devAddr:resource.ocDevAddr values:values connType:resource.connectivityType qos:qos callback:callback];
}

/**
 * @discussion This method puts values to a CoAP resoruce using the default
 connectivity type and quality of service.
 * @param uri The URI of the resource to perform this transaction on
 * @param devAddr An OCDevAddr struct representing the target device
 * @param values An array of values to PUT to the resource
 * @param connType The connectivity type to perform this transaction over.
 This value should include one adapter enum flag (CT_ADAPTER_XXX).
 * @param qos The quality of service to use for this transaction.
 * @param callback The callback called when the transaction has finished.
 * @return OCStackResult status code returned by OCDoResource
 */
- (int) putResource:(NSString*)uri
            devAddr:(OCDevAddr)devAddr
             values:(NSArray<OcRepresentationValue*>*)values
           connType:(OCConnectivityType)connType
                qos:(OCQualityOfService)qos
           callback:(PutCallback)callback
{
    NSString* address = [NSString stringWithUTF8String:devAddr.addr];
    NSLog(@"Calling PUT on: %@ %@", address, uri);
    if ([uri length] == 0) {
        NSLog(@"ERROR: input uri is invalid");
        return OC_STACK_ERROR;
    } else if ([address length] == 0) {
        NSLog(@"ERROR: input address is invalid");
        return OC_STACK_ERROR;
    }
    OCDoHandle *handle = malloc(CA_MAX_TOKEN_LEN);
    arc4random_buf(handle, CA_MAX_TOKEN_LEN);
    NSData *dataHandle = [NSData dataWithBytes:handle length:CA_MAX_TOKEN_LEN];
    // Add callback to dictionary
    [_putCallbacks setObject:callback forKey:dataHandle];
    
    // Set callback
    OCCallbackData cb = {
        .cb = oc_put_cb
    };

    OCRepPayload *repPayload = OCRepPayloadCreate();
    // Create OCRepPayload from input values
    for (OcRepresentationValue* value in values) {
        switch (value.type) {
            case OCREP_PROP_NULL:
                OCRepPayloadSetNull(repPayload, [value.name UTF8String]);
                break;
            case OCREP_PROP_INT:
                OCRepPayloadSetPropInt(repPayload, [value.name UTF8String], value.intValue);
                break;
            case OCREP_PROP_DOUBLE:
                OCRepPayloadSetPropDouble(repPayload, [value.name UTF8String], value.doubleValue);
                break;
            case OCREP_PROP_BOOL:
                OCRepPayloadSetPropBool(repPayload, [value.name UTF8String], value.boolValue);
                break;
            case OCREP_PROP_STRING:
                OCRepPayloadSetPropString(repPayload, [value.name UTF8String], [value.stringValue UTF8String]);
                break;
            case OCREP_PROP_BYTE_STRING:
            {
                OCByteString byteString = {
                    .bytes = value.byteStringValue,
                    .len = value.byteStringLength
                };
                OCRepPayloadSetPropByteString(repPayload, [value.name UTF8String], byteString);
            }
                break;
            case OCREP_PROP_OBJECT:
                // XXX TODO Test
                NSLog(@"WARNING: Not tested");
                OCRepPayloadSetPropObject(repPayload, [value.name UTF8String], value.objectValue.ocRepPayload);
                break;
            case OCREP_PROP_ARRAY:
                // XXX TODO Test
                NSLog(@"WARNING: Not tested");
                int arraySize = (int)[value.arrayValue count];
                const OCRepPayload** representationArray = malloc(arraySize);
                for (int i = 0; i < arraySize; i++) {
                    OCRepPayload* repPayload = value.arrayValue[i].ocRepPayload;
                    representationArray[i] = repPayload;
                }
                OCRepPayloadSetPropObjectArray(repPayload, [value.name UTF8String], representationArray, (size_t*) &arraySize);
                free(representationArray);
                break;
            default:
                NSLog(@"ERROR: OcRepresentationValue type did not match any known type;");
                break;
        }
    }
    return OCDoResource(handle, OC_REST_PUT, [uri UTF8String], &devAddr, (OCPayload *)repPayload,
                        connType, qos, &cb, NULL, 0);
}

// PUT Callback
static OCStackApplicationResult
oc_put_cb(void *ctx, OCDoHandle handle, OCClientResponse *rsp){
    // Get shared IotivityClient object
    IotivityClient *iot = [IotivityClient shared];
    
    // Make sure OCClientResponse is not nil
    if (!rsp) {
        NSLog(@"ERROR: OCClientResponse is nil!");
        return OC_STACK_DELETE_TRANSACTION;
    }
    
    // Get address and uri from OCClientResponse object
    NSString* address = [NSString stringWithUTF8String:rsp->addr->addr];
    NSString* uri = [NSString stringWithUTF8String:rsp->resourceUri];
    NSLog(@"PUT callback from: %@ %@", address, uri);
    
    // If the response payload is not nil, construct an OcRepresentation to be passed to callback
    OcRepresentation *ocRepresentation = nil;
    if (!rsp->payload) {
        NSLog(@"WARN: OCClientResponse->payload is nil!");
        ocRepresentation = [[OcRepresentation alloc] init:rsp:nil];
    } else if (rsp->payload->type == PAYLOAD_TYPE_REPRESENTATION) {
        OCRepPayload *representation_payload = (OCRepPayload *)rsp->payload;
        ocRepresentation = [[OcRepresentation alloc] init:rsp:representation_payload];
    }
    
    // Get callback from the put callback dictionary
    NSData* data = [NSData dataWithBytes:handle length:CA_MAX_TOKEN_LEN];
    PutCallback callback = [iot.putCallbacks objectForKey:data];
    if (callback != nil) {
        // If the callback exists, call the callback on the main thread
        dispatch_async(dispatch_get_main_queue(), ^{
            callback(ocRepresentation);
            [iot.putCallbacks removeObjectForKey:data];
            [ocRepresentation autorelease];
        });
    } else {
        NSLog(@"ERROR: Callback not found!");
    }
    
    return OC_STACK_DELETE_TRANSACTION;
}

///////////////////////////////////////////////////////////////////////////////
//  OBSERVE
///////////////////////////////////////////////////////////////////////////////

/**
 * @discussion This method periodically observes a CoAP resource at an interval
 specified by the host.
 * @param uri The URI of the resource to perform this transaction on
 * @param address The address of the device hosting the resource
 * @param callback The callback called when the transaction has finished.
 * @return OCStackResult status code returned by OCDoResource
 */
- (int) observeResource:(NSString*)uri
                address:(NSString*)address
               callback:(ObserveCallback)callback
{
    return [self observeResource:uri address:address connType:_defaultConnType qos:_defaultQos callback:callback];
}

/**
 * @discussion This method periodically observes a CoAP resource at an interval
 specified by the host.
 * @param uri The URI of the resource to perform this transaction on
 * @param address The address of the device hosting the resource
 * @param connType The connectivity type to perform this transaction over.
 This value should include one adapter enum flag (CT_ADAPTER_XXX).
 * @param qos The quality of service to use for this transaction.
 * @param callback The callback called when the transaction has finished.
 * @return OCStackResult status code returned by OCDoResource
 */
- (int) observeResource:(NSString*)uri
                address:(NSString*)address
               connType:(OCConnectivityType)connType
                    qos:(OCQualityOfService)qos
               callback:(ObserveCallback)callback
{
    // Create a devAddr and set the adapter and address
    OCDevAddr devAddr = {
        .adapter = connType >> CT_ADAPTER_SHIFT
    };
    strcpy(devAddr.addr,[address UTF8String]);
    
    return [self observeResource:uri devAddr:devAddr connectivityType:connType qos:qos callback:callback];
}

/**
 * @discussion This method periodically observes an OCF resource at an interval
 specified by the host.
 * @param resource An OcResource object representing an OCF resource.
 * @param qos The quality of service to use for this transaction.
 * @param callback The callback called when the transaction has finished.
 * @return OCStackResult status code returned by OCDoResource
 */
- (int) observeResource:(OcResource*)resource
                    qos:(OCQualityOfService)qos
               callback:(ObserveCallback)callback
{
    return [self observeResource:resource.uri devAddr:resource.ocDevAddr connectivityType:resource.connectivityType qos:qos callback:callback];
}

/**
 * @discussion This method periodically observes an OCF resource at an interval
 specified by the host.
 * @param uri The URI of the resource to perform this transaction on
 * @param devAddr An OCDevAddr struct representing the target device
 * @param connType The connectivity type to perform this transaction over.
 This value should include one adapter enum flag (CT_ADAPTER_XXX).
 * @param qos The quality of service to use for this transaction.
 * @param callback The callback called when the transaction has finished.
 * @return OCStackResult status code returned by OCDoResource
 */
- (int) observeResource:(NSString*)uri
                devAddr:(OCDevAddr)devAddr
       connectivityType:(OCConnectivityType)connType
                    qos:(OCQualityOfService)qos
               callback:(ObserveCallback)callback
{
    NSString* address = [NSString stringWithUTF8String:devAddr.addr];
    NSLog(@"Calling OBSERVE on: %@ %@", address, uri);
    if ([uri length] == 0) {
        NSLog(@"ERROR: input uri is invalid");
        return OC_STACK_ERROR;
    } else if ([address length] == 0) {
        NSLog(@"ERROR: input address is invalid");
        return OC_STACK_ERROR;
    }
    
    // Create a random handle to use to identify the callback to use
    void *handle = malloc(CA_MAX_TOKEN_LEN);
    arc4random_buf(handle, CA_MAX_TOKEN_LEN);
    NSData *dataHandle = [NSData dataWithBytes:handle length:CA_MAX_TOKEN_LEN];
    // Add callback to dictionary
    [_observeCallbacks setObject:callback forKey:dataHandle];
    // Add handle to dictionary so it can be used to cancel the observe
    [_observeHandles setObject:[NSValue valueWithPointer:handle] forKey:[NSString stringWithFormat:@"%@%@", [address lowercaseString], uri]];
    
    // Set callback
    OCCallbackData cb = {
        .cb = oc_observe_cb
    };

    return OCDoResource(handle, OC_REST_OBSERVE, [uri UTF8String], &devAddr, NULL,
                        connType, qos, &cb, NULL, 0);
}

// Observe Callback
static OCStackApplicationResult
oc_observe_cb(void *ctx, OCDoHandle handle, OCClientResponse *rsp) {
    // Get shared IotivityClient object
    IotivityClient *iot = [IotivityClient shared];
    
    // Make sure OCClientResponse is not nil
    if (!rsp) {
        NSLog(@"ERROR: OCClientResponse is nil!");
        return OC_STACK_DELETE_TRANSACTION;
    }
    
    // Get address and uri from OCClientResponse object
    NSString* address = [NSString stringWithUTF8String:rsp->addr->addr];
    NSString* uri = [NSString stringWithUTF8String:rsp->resourceUri];
    NSLog(@"OBSERVE callback from: %@ %@", address, uri);
    
    // Flag to determine to keep or delete the iotivity transaction
    bool keepTransaction = true;
    
    // If the response payload is not nil, construct an OcRepresentation to be passed to callback
    OcRepresentation *ocRepresentation = nil;
    if (!rsp->payload) {
        NSLog(@"WARN: OCClientResponse->payload is nil!");
        keepTransaction = false;
        ocRepresentation = [[OcRepresentation alloc] init:rsp:nil];
    } else if (rsp->payload->type == PAYLOAD_TYPE_REPRESENTATION) {
        OCRepPayload *representation_payload = (OCRepPayload *)rsp->payload;
        ocRepresentation = [[OcRepresentation alloc] init:rsp:representation_payload];
    }
    
    // Get callback from the get callback dictionary
    NSData* data = [NSData dataWithBytes:handle length:CA_MAX_TOKEN_LEN];
    ObserveCallback callback = [iot.observeCallbacks objectForKey:data];
    if (callback != nil) {
        // If the callback exists, call the callback on the main thread
        dispatch_async(dispatch_get_main_queue(), ^{
            callback(ocRepresentation);
            [ocRepresentation autorelease];
        });
    } else {
        NSLog(@"ERROR: Callback not found!");
    }
    
    // If callback did not return error, keep the transaction to continue to observe the
    // resource. If the callback resulted in an error, delete the transaction.
    if (keepTransaction) {
        return OC_STACK_KEEP_TRANSACTION;
    } else {
        return OC_STACK_DELETE_TRANSACTION;
    }
}

/**
 * @discussion Cancel an ongoing observe transaction
 * @param uri The URI of the resource to cancel observation on
 * @param address The host address of the resource
 * @return An OCStackResult returned by OCCancel
 */
- (int) cancelObserve:(NSString *)uri address:(NSString*)address
{
    // Get handle and cancel the observe
    NSValue *handleValue = [_observeHandles objectForKey:[NSString stringWithFormat:@"%@%@", [address lowercaseString], uri]];
    OCDoHandle handle = [handleValue pointerValue];
    return OCCancel(handle, OC_LOW_QOS, NULL, 0);
}

/**
 * @discussion Cancel an ongoing observe transaction
 * @param resource The resource to cancel observation on
 * @return An OCStackResult returned by OCCancel
 */
- (int) cancelObserve:(OcResource*)resource
{
    // Get handle and cancel the observe
    NSValue *handleValue = [_observeHandles objectForKey:[NSString stringWithFormat:@"%@%@", [resource.address lowercaseString], resource.uri]];
    OCDoHandle handle = [handleValue pointerValue];
    return OCCancel(handle, OC_LOW_QOS, NULL, 0);
}

@end

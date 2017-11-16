
#import <Foundation/Foundation.h>
#import "octypes.h"
#import "Iotivity.h"

/**
 * OcRepresentation is a OCF representation of a resource obtained via REST
 * calls (i.e. GET, PUT, etc.) to a CoAP resource. This representation 
 * contains all the infomration that an OcResource object contains, as well
 * as a dictionary of values. The values corresponding to the key strings in
 * the "values" dictionary are one of the following types:
 *
 *  1. NULL
 *
 *  2. int64_t
 *
 *  3. double
 *
 *  4. bool
 *
 *  5. NSString*
 *
 *  6. uint8_t* (byte string)
 *
 *  7. OcRepresentation* (object)
 *
 *  8. NSArray<OcRepresentation*>*
 */
@interface OcRepresentation : NSObject

/// The device address of this resource's host. Most importantly, this struct contains 
/// information about the host's address and transport adapter among other things. 
@property OCDevAddr ocDevAddr;
/// Iotivity's internal payload structure. This can generally be ignored.
@property OCRepPayload * ocRepPayload;
/// Result of the transaction that obtained this representation.
@property OCStackResult result;
/// Adapter type for this transaction
@property OCTransportAdapter adapterType;
/// Connectivity type for this transaction
@property OCConnectivityType connectivityType;
/// The security identity of the remote server
@property OCIdentity identity;
/// The sequence number of this transaction. This will represent the sequence of 
/// notifications from a server.
@property uint32_t sequenceNumber;
/// Address of the resource's host
@property(strong) NSString *address;
/// URI of the resource corresponding to this representation.
@property(strong) NSString *uri;
/// Resource types of the resource corresponding to this representation
@property(strong) NSMutableArray *resourceTypes;
/// Resource interfaces of the resource corresponding to this representation
@property(strong) NSMutableArray *resourceInterfaces;
/// Dictionary of the resource's values. See OcRepresentation or OcRepresentationValue
/// for more detail on possible values.
@property(strong) NSMutableDictionary<NSString*, id> *values;

/**
 * Initialize an OcRepresentation obeject using the structures obtained by an ocstack
 * callback
 */
- (instancetype)init:(OCClientResponse *) rsp :(OCRepPayload *) representation;

@end



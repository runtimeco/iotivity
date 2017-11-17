
#import <Foundation/Foundation.h>
#import "octypes.h"
#import "ocstack.h"
#import "Iotivity.h"
#import "IotivityClient.h"

/**
 * OcResource represents a CoAP resource in the context of OCF (OIC). This means that
 * OcResources are generally obtained through resource discovery of a CoAP host using
 * one of the resource discovery methods contained in IotivityClient.
 * 
 * Additionally, these OCF resources contain resource types and interfaces which are
 * used to further identify their purpose. For example a light switch resource could
 * conform to the binary switch resource type defined in the OCF Resource Types 
 * specification, which defines a resource type of "oic.r.switch.binary".
 * 
 * Finally, resources contain any number of values which are described more in the
 * OcRepresentation and OcRepresentationValue classes. 
 *
 * Once a resource is discovered using resource discovery, an applicaiton can perform
 * a number of RESTful style api calls on the resource (i.e. GET, PUT, etc.) in order
 * obtain information or modify the resource's values.
 */
@interface OcResource : NSObject

/// The device address of this resource's host. Most importantly, this struct contains 
/// information about the host's address and transport adapter among other things. 
@property (nonatomic) OCDevAddr ocDevAddr;

/// Address for this resource's endpoint. Format varies depending on connectivity type
@property (nonatomic, strong) NSString *address;
/// Resource URI
@property (nonatomic, strong) NSString *uri;
/// Resource types
@property (nonatomic, strong) NSMutableArray<NSString*> *resourceTypes;
/// Resource interfaces
@property (nonatomic, strong) NSMutableArray<NSString*> *resourceInterfaces;
/// Adapter type enum
@property (nonatomic) OCTransportAdapter adapterType;
/// Connectivity type enum
@property (nonatomic) OCConnectivityType connectivityType;

/**
 * @discussion Initialize this resource using an OCClientResponse and an
 OCResourcePayload from the ocstack callback.
 * @param rsp The OCClientResponse from the ocstack callback.
 * @param resource The OCResourcePayload from the ocstack callback's payload.
 * @return An OcResource object.
 */
- (instancetype)init:(OCClientResponse *)rsp :(OCResourcePayload *) resource;

/**
 * @discussion Get this resource's values using the default IotivityClient QoS.
 * @param callback The callback triggered when this transaction has finished.
 * @return An OCStackResult returned from OCDoResource.
 */
- (int) get:(GetCallback)callback;

/**
 * @discussion Get this resource's values using a specific QoS.
 * @param qos The quality of service for this transaction
 * @param callback The callback triggered when this transaction has finished.
 * @return An OCStackResult returned from OCDoResource.
 */
- (int) get:(OCQualityOfService)qos callback:(GetCallback)callback;

/**
 * @discussion PUT values to this resource using the default QoS (low).
 * @param values An array of values to PUT to the resource
 * @param callback The callback triggered when this transaction has finished.
 * @return An OCStackResult returned from OCDoResource.
 */
- (int) put:(NSArray<OcRepresentationValue*>*)values callback:(PutCallback)callback;

/**
 * @discussion PUT values to this resource using a specific QoS.
 * @param values An array of values to PUT to the resource
 * @param qos The quality of service for this transaction
 * @param callback The callback triggered when this transaction has finished.
 * @return An OCStackResult returned from OCDoResource.
 */
- (int) put:(NSArray<OcRepresentationValue*>*)values qos:(OCQualityOfService)qos callback:(PostCallback)callback;

/**
 * @discussion POST values to this resource using the default QoS (low).
 * @param values An array of values to POST to the resource
 * @param callback The callback triggered when this transaction has finished.
 * @return An OCStackResult returned from OCDoResource.
 */
- (int) post:(NSArray<OcRepresentationValue*>*)values callback:(PostCallback)callback;

/**
 * @discussion POST values to this resource using a specific QoS.
 * @param values An array of values to POST to the resource
 * @param qos The quality of service for this transaction
 * @param callback The callback triggered when this transaction has finished.
 * @return An OCStackResult returned from OCDoResource.
 */
- (int) post:(NSArray<OcRepresentationValue*>*)values qos:(OCQualityOfService)qos callback:(PostCallback)callback;

/**
 * @discussion This method periodically observes this resource at an interval
 determined by the host.
 * @param qos The quality of service to use for this transaction.
 * @param callback The callback called when the transaction has finished.
 * @return OCStackResult status code returned by OCDoResource
 */
- (int) observe:(ObserveCallback)callback;

/**
 * @discussion This method periodically observes this resource at an interval
 determined by the host.
 * @param qos The quality of service to use for this transaction.
 * @param callback The callback called when the transaction has finished.
 * @return OCStackResult status code returned by OCDoResource
 */
- (int) observe:(OCQualityOfService)qos callback:(ObserveCallback)callback;

/**
 * @discussion This method cancel's an observe transaction
 * @return OCStackResult status code returned by OCCancel
 */
- (int) cancelObserve;
@end

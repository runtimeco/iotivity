
#import <Foundation/Foundation.h>

@class IotivityClient;
@class OcResource;
@class OcRepresentation;
@class OcRepresentationValue;

/**
 * The callback closure for resource discovery.
 */
typedef void (^DiscoverCallback)(NSArray<OcResource*>* resources);
/**
 * The callback closure for GET operations on a resource.
 */
typedef void (^GetCallback)(OcRepresentation* representation);
/**
 * The callback closure for GET operations on a resource.
 */
typedef void (^PutCallback)(OcRepresentation* representation);
/**
 * The callback closure for OBSERVE operations on a resource. This callback
 * will be called at a frequencey determined by the server until the 
 * operation is cancelled.
 */
typedef void (^ObserveCallback)(OcRepresentation* representation);

#define IOTIVITY_UUID "ADE3D529-C784-4F63-A987-EB69F70EE816"

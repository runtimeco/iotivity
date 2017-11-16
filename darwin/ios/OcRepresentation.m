//
//  OcRepresentation.m
//  cigatewayconfig
//
//  Created by Brian Giori on 8/15/17.
//  Copyright © 2017 Brian Giori. All rights reserved.
//
#import "OcRepresentation.h"

@implementation OcRepresentation

- (instancetype) init:(OCClientResponse *) response :(OCRepPayload *) representation {
    // Set the ocDevAddr for indentifying the endpoint
    _ocDevAddr = response->devAddr;
    
    // Set the ocRepPayload
    _ocRepPayload = representation;
    
    // Set the OCStackResult field
    _result = response->result;
    
    // Resource address and URI
    _address = [NSString stringWithUTF8String:response->devAddr.addr];
    _uri = [NSString stringWithUTF8String:response->resourceUri];
    
    // If our representaiton is nil, we still want to create a new OcRepresentation
    // object with our address and uri for retransmission purposes.
    if (representation == nil) {
        _resourceTypes = nil;
        _resourceInterfaces = nil;
        _values = nil;
        return self;
    }
    
    // Allocate resource type array and add values
    _resourceTypes = [[NSMutableArray alloc] init];
    for (OCStringLL * type = representation->types; type; type = type->next) {
        [_resourceTypes addObject:[NSString stringWithUTF8String:type->value]];
    }
    // Allocate resource interfaces array and add values
    _resourceInterfaces = [[NSMutableArray alloc] init];
    for (OCStringLL * iface = representation->interfaces; iface; iface = iface->next) {
        [_resourceInterfaces addObject:[NSString stringWithUTF8String:iface->value]];
    }
    
    // Set the values from the representation payload
    _values = [[NSMutableDictionary alloc] init];
    OCRepPayloadValue *rep_val;
    
    NSLog(@"{");
    for (rep_val = representation->values; rep_val; rep_val = rep_val->next) {
        NSString * name = [NSString stringWithUTF8String:rep_val->name];
        if (rep_val->type == OCREP_PROP_NULL) {
            // Null
            NSLog(@"\t%@=nil", name);
        } else if (rep_val->type == OCREP_PROP_INT) {
            // Int
            NSLog(@"\t%@=%@", name, [NSNumber numberWithLongLong:rep_val->i]);
            [_values setObject:[NSNumber numberWithLongLong:rep_val->i] forKey:name];
        } else if (rep_val->type == OCREP_PROP_DOUBLE) {
            // Double
            NSLog(@"\t%@=%@", name, [NSNumber numberWithDouble:rep_val->d]);
            [_values setObject:[NSNumber numberWithDouble:rep_val->d] forKey:name];
        } else if (rep_val->type == OCREP_PROP_BOOL) {
            // Bool
            NSLog(@"\t%@=%@", name, [NSNumber numberWithBool:rep_val->b]);
            [_values setObject:[NSNumber numberWithBool:rep_val->b] forKey:name];
        } else if (rep_val->type == OCREP_PROP_STRING) {
            // String
            NSLog(@"\t%@=%@", name, [NSString stringWithUTF8String:rep_val->str]);
            [_values setObject:[NSString stringWithUTF8String:rep_val->str] forKey:name];
        } else if (rep_val->type == OCREP_PROP_BYTE_STRING) {
            // Byte String
            NSData* bytes = [NSData dataWithBytes:rep_val->ocByteStr.bytes length:rep_val->ocByteStr.len];
            NSLog(@"\t%@=%@", name, bytes.description);
            [_values setObject:bytes forKey:name];
        } else if (rep_val->type == OCREP_PROP_OBJECT) {
            // Object
            NSLog(@"\t%@=", name);
            OcRepresentation *ocRep = [[OcRepresentation alloc] init:response:rep_val->obj];
            [_values setObject:ocRep forKey:name];
        } else if (rep_val->type == OCREP_PROP_ARRAY) {
            // Array
            NSLog(@"\t%@=[", name);
            NSMutableArray *arr = [[NSMutableArray alloc] init];
            if (rep_val->arr.type == OCREP_PROP_INT) {
                // Integer array
                for (int i = 0; i < (int)rep_val->ocByteStr.len; i++){
                    NSLog(@"\t\t%@", [NSNumber numberWithLongLong:rep_val->arr.iArray[i]]);
                    [arr addObject:[NSNumber numberWithLongLong:rep_val->arr.iArray[i]]];
                }
                [_values setObject:arr forKey:[NSString stringWithUTF8String:rep_val->name]];
            } else if (rep_val->arr.type == OCREP_PROP_DOUBLE) {
                // Double array
                for (int i = 0; i < (int)rep_val->ocByteStr.len; i++){
                    NSLog(@"\t\t%@", [NSNumber numberWithDouble:rep_val->arr.dArray[i]]);
                    [arr addObject:[NSNumber numberWithDouble:rep_val->arr.dArray[i]]];
                }
                [_values setObject:arr forKey:[NSString stringWithUTF8String:rep_val->name]];
            } else if (rep_val->arr.type == OCREP_PROP_BOOL) {
                // Boolean array
                for (int i = 0; i < (int)rep_val->ocByteStr.len; i++){
                    NSLog(@"\t\t%@", [NSNumber numberWithBool:rep_val->arr.bArray[i]]);
                    [arr addObject:[NSNumber numberWithBool:rep_val->arr.bArray[i]]];
                }
                [_values setObject:arr forKey:[NSString stringWithUTF8String:rep_val->name]];
            } else if (rep_val->arr.type == OCREP_PROP_STRING) {
                // String array
                for (int i = 0; i < (int)rep_val->ocByteStr.len; i++){
                    NSLog(@"\t\t%@", [NSString stringWithUTF8String:rep_val->arr.strArray[i]]);
                    [arr addObject:[NSString stringWithUTF8String:rep_val->arr.strArray[i]]];
                }
                [_values setObject:arr forKey:[NSString stringWithUTF8String:rep_val->name]];
            } else if (rep_val->arr.type == OCREP_PROP_BYTE_STRING) {
                // Byte string array (TODO TEST)
                for (int i = 0; i < (int)rep_val->ocByteStr.len; i++){
                    NSData* bytes = [NSData dataWithBytes:rep_val->arr.ocByteStrArray[i].bytes
                                                   length:rep_val->arr.ocByteStrArray[i].len];
                    NSLog(@"\t%@=%@", name, bytes.description);
                    [arr addObject:bytes];
                }
                [_values setObject:arr forKey:[NSString stringWithUTF8String:rep_val->name]];
            } else if (rep_val->arr.type == OCREP_PROP_OBJECT) {
                // Object Array
                for (int i = 0; i < (int)rep_val->ocByteStr.len; i++){
                    OcRepresentation *ocRep = [[OcRepresentation alloc] init:response:rep_val->arr.objArray[i]];                    
                    [arr addObject:ocRep];
                }
                [_values setObject:arr forKey:[NSString stringWithUTF8String:rep_val->name]];
            }
            NSLog(@"\t]");
        }
    }
    NSLog(@"}");

    
    return self;
}

@end

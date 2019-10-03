//
//  NkeConnection.cpp
//  NkeClient
//
//  Created by slava on 25/12/2016.
//  Copyright (c) 2016 Slava-Imameev. All rights reserved.
//

#include "NkeConnection.h"
//#include <CoreFoundation/CoreFoundation.h>

//--------------------------------------------------------------------

//
// the returned connection must be closed by calling IOServiceClose
//
kern_return_t
NkeOpenDlDriver(
                io_connect_t*    connection
                )
{
    kern_return_t   kr;
    io_iterator_t   iterator;
    io_service_t    serviceObject;
    CFDictionaryRef classToMatch;
    
    setbuf(stdout, NULL);
    
    // Problem may be service matching!before
    if (!(classToMatch = IOServiceMatching("NetworkKernelExtension"))){
        printf("failed to create matching dictionary\n");
        return kIOReturnError;
    }
    
//    printf("Dictionary Count: %ld \n", CFDictionaryGetCount(classToMatch));
    
    //
    // IOServiceGetMatchingServices consumes classToMatch rseference
    //
//    printf("Master Port Default: %d\n", kIOMasterPortDefault);
    
    kr = IOServiceGetMatchingServices(kIOMasterPortDefault, classToMatch,
                                      &iterator);
    if (kr != kIOReturnSuccess){
        
        printf("failed to retrieve matching services\n");
        return kr;
    }
    
//    if (IOIteratorIsValid(iterator)) {
//        printf("Iterator is valid!\n");
//    } else {
//        printf("Iterator is not valid!\n");
//    }
    
    serviceObject = IOIteratorNext(iterator);
    
    IOObjectRelease(iterator);
    if (!serviceObject){
        
        printf("NetworkKernelExtension service not found\n");
        return kIOReturnError;
    }
    
    kr = IOServiceOpen( serviceObject, mach_task_self(), kNkeUserClientCookie, connection );
    IOObjectRelease(serviceObject);
    if (kr != kIOReturnSuccess){
        
        printf("failed to open NetworkKernelExtension service, an error is %i \n", kr);
        return kr;
    }
    
    kr = IOConnectCallScalarMethod( *connection, kt_NkeUserClientOpen, NULL, 0, NULL, NULL);
    if (kr != KERN_SUCCESS) {
        (void)IOServiceClose( *connection );
        printf(("NetworkKernelExtension service is busy\n"));
        return kr;
    }
    
    return kr;
    
}

//--------------------------------------------------------------------



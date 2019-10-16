/*
 * Copyright (c) 2016 Slava Imameev. All rights reserved.
 */

#ifndef __NETWORKKERNELEXTENSION_H__
#define __NETWORKKERNELEXTENSION_H__

#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <sys/ioccom.h> // ioctl macros

#define NKE_START_DIVERTING _IO('F', 1)
#define NKE_STOP_DIVERTING _IO('F', 2)

#include "NkeCommon.h"

//--------------------------------------------------------------------

// I/O Kit driver class
class NetworkKernelExtension : public IOService
{
    OSDeclareDefaultStructors(NetworkKernelExtension)
    
public:
    virtual bool start(IOService *provider);
    virtual void stop( IOService * provider );
    int ioctl( dev_t dev, u_long cmd, caddr_t data, int fflag, struct thread *td);
    
    
    virtual IOReturn newUserClient( __in task_t owningTask,
                                    __in void*,
                                    __in UInt32 type,
                                    __in IOUserClient **handler );

    static NetworkKernelExtension*  getInstance(){ return NetworkKernelExtension::Instance; };
    
protected:
    
    virtual bool init();
    virtual void free();
    
private:
    
    static NetworkKernelExtension* Instance;
    
};

//--------------------------------------------------------------------

#endif//__NETWORKKERNELEXTENSION_H__
